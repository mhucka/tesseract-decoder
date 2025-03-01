// Copyright 2025 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "tesseract.h"

#include <algorithm>
#include <cassert>
#include <iostream>

bool Node::operator>(const Node& other) const {
  return cost > other.cost || (cost == other.cost && num_dets < other.num_dets);
}

double TesseractDecoder::get_detcost(
    size_t d, const std::vector<bool>& blocked_errs, const std::vector<size_t>& det_counts,
    const std::vector<bool>& dets) const {
  double min_cost = INF;
  double min_cost_clean = INF;
  assert(dets[d]);
  for (size_t ei : d2e[d]) {
    if (!blocked_errs[ei]) {
      double ecost = (errors[ei].likelihood_cost) / det_counts[ei];
      min_cost = std::min(min_cost, ecost);
      assert(det_counts[ei]);
      if (edets[ei].size() > det_counts[ei]) continue;
      min_cost_clean = std::min(min_cost_clean, ecost);
    }
  }
  if (config.detcost_prefer_clean and min_cost_clean < INF) return min_cost_clean;

  return min_cost;
}

TesseractDecoder::TesseractDecoder(TesseractConfig config_) : config(config_) {
  if (config.det_orders.empty()) {
    config.det_orders.emplace_back(config.dem.count_detectors());
    std::iota(config.det_orders[0].begin(), config.det_orders[0].end(), 0);
  } else {
    for (size_t i = 0; i < config.det_orders.size(); ++i) {
      assert(config.det_orders[i].size() == config.dem.count_detectors());
    }
  }
  assert(this->config.det_orders.size());
  errors = get_errors_from_dem(config.dem.flattened());
  num_detectors = config.dem.count_detectors();
  num_errors = config.dem.count_errors();
  initialize_structures(config.dem.count_detectors());
}

void TesseractDecoder::initialize_structures(size_t num_detectors) {
  d2e.resize(num_detectors);
  edets.resize(num_errors);

  for (size_t ei = 0; ei < num_errors; ++ei) {
    edets[ei] = errors[ei].symptom.detectors;
    for (int d : edets[ei]) {
      d2e[d].push_back(ei);
    }
  }

  eneighbors.resize(num_errors);
  for (size_t ei = 0; ei < num_errors; ++ei) {
    std::set<int> neighbor_set;
    for (int d : edets[ei]) {
      for (int oei : d2e[d]) {
        for (int od : edets[oei]) {
          if (std::find(edets[ei].begin(), edets[ei].end(), od) == edets[ei].end()) {
            neighbor_set.insert(od);
          }
        }
      }
    }
    eneighbors[ei] = std::vector<int>(neighbor_set.begin(), neighbor_set.end());
  }
}

struct VectorBoolHash {
  size_t operator()(const std::vector<bool>& v) const {
    std::hash<bool> bool_hash;
    size_t seed = 0;
    for (bool b : v) {
      // Combine hash values of individual booleans.
      // A simple way is to use XOR and bit shifting,
      // but you can use other combining strategies as well.
      seed ^= bool_hash(b) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
    return seed;
  }
};

void TesseractDecoder::decode_to_errors(const std::vector<size_t>& detections) {
  std::vector<size_t> best_errors;
  double best_cost = std::numeric_limits<double>::max();
  assert(config.det_orders.size());
  int max_det_beam = config.det_beam;
  if (config.beam_climbing) {
    for (int beam = 0; beam <= max_det_beam; ++beam) {
      config.det_beam = beam;
      size_t det_order = beam % config.det_orders.size();
      decode_to_errors(detections, det_order);
      double this_cost = cost_from_errors(predicted_errors_buffer);
      if (!low_confidence_flag and this_cost < best_cost) {
        best_errors = predicted_errors_buffer;
        best_cost = this_cost;
      }
      if (config.verbose) {
        std::cout << "for det_order " << det_order << " beam " << beam << " got low confidence "
                  << low_confidence_flag << " and cost " << this_cost << " and obs_mask "
                  << mask_from_errors(predicted_errors_buffer)
                  << ". Best cost so far: " << best_cost << std::endl;
      }
    }
  } else {
    for (size_t det_order = 0; det_order < config.det_orders.size(); ++det_order) {
      decode_to_errors(detections, det_order);
      double this_cost = cost_from_errors(predicted_errors_buffer);
      if (!low_confidence_flag and this_cost < best_cost) {
        best_errors = predicted_errors_buffer;
        best_cost = this_cost;
      }
      if (config.verbose) {
        std::cout << "for det_order " << det_order << " beam " << config.det_beam
                  << " got low confidence " << low_confidence_flag << " and cost " << this_cost
                  << " and obs_mask " << mask_from_errors(predicted_errors_buffer)
                  << ". Best cost so far: " << best_cost << std::endl;
      }
    }
  }
  config.det_beam = max_det_beam;
  predicted_errors_buffer = best_errors;
  low_confidence_flag = (best_cost == std::numeric_limits<double>::max());
}

bool QNode::operator>(const QNode& other) const {
  return cost > other.cost || (cost == other.cost && num_dets < other.num_dets);
}

Node TesseractDecoder::to_node(
    const QNode& qnode, const std::vector<bool>& shot_dets, size_t det_order) const {
  Node node;
  node.cost = qnode.cost;
  node.errs = qnode.errs;
  node.num_dets = qnode.num_dets;

  // Reconstruct the dets and blocked_errs
  node.dets = shot_dets;
  node.blocked_errs.resize(num_errors, false);
  for (size_t ei : node.errs) {
    // Get the min index activated detector before updating the dets
    size_t min_det = std::numeric_limits<size_t>::max();
    for (size_t d = 0; d < num_detectors; ++d) {
      if (node.dets[config.det_orders[det_order][d]]) {
        min_det = config.det_orders[det_order][d];
        break;
      }
    }
    // Reconstruct the blocked_errs
    for (size_t oei : d2e[min_det]) {
      node.blocked_errs[oei] = true;
      if (!config.at_most_two_errors_per_detector and oei == ei) break;
    }

    // Reconstruct the dets
    for (size_t d : edets[ei]) {
      if (node.dets[d]) {
        node.dets[d] = false;
        if (config.at_most_two_errors_per_detector) {
          for (size_t oei : d2e[d]) {
            node.blocked_errs[oei] = true;
          }
        }
      } else {
        node.dets[d] = true;
      }
      // node.dets[d] = !node.dets[d];
    }
  }

  return node;
}

void TesseractDecoder::decode_to_errors(const std::vector<size_t>& detections, size_t det_order) {
  size_t det_beam = config.det_beam;
  predicted_errors_buffer.clear();
  low_confidence_flag = false;
  std::vector<bool> dets(num_detectors, false);
  for (size_t d : detections) {
    dets[d] = true;
  }

  std::priority_queue<QNode, std::vector<QNode>, std::greater<QNode>> pq;
  std::unordered_map<size_t, std::unordered_set<std::vector<bool>, VectorBoolHash>> discovered_dets;

  size_t min_num_dets;
  {
    std::vector<size_t> errs;
    std::vector<bool> blocked_errs(num_errors, false);
    std::vector<size_t> det_counts(num_errors, 0);

    for (size_t d = 0; d < num_detectors; ++d) {
      if (!dets[d]) continue;
      for (int ei : d2e[d]) {
        det_counts[ei]++;
      }
    }
    double initial_cost = 0.0;
    for (size_t d = 0; d < num_detectors; ++d) {
      if (!dets[d]) continue;
      initial_cost += get_detcost(d, blocked_errs, det_counts, dets);
    }
    if (initial_cost == INF) {
      low_confidence_flag = true;
      return;
    }
    min_num_dets = static_cast<size_t>(std::count(dets.begin(), dets.end(), true));
    // pq.push({errs, dets, initial_cost, min_num_dets, blocked_errs});
    pq.push({initial_cost, min_num_dets, errs});
  }
  size_t num_pq_pushed = 1;

  size_t max_num_dets = min_num_dets + det_beam;
  while (!pq.empty()) {
    const QNode qnode = pq.top();
    if (qnode.num_dets > max_num_dets) {
      pq.pop();
      continue;
    }
    const Node node = to_node(qnode, dets, det_order);
    pq.pop();

    if (node.num_dets == 0) {
      if (config.verbose) {
        std::cout.precision(13);
        std::cout << "Decoding complete. Cost: " << node.cost
                  << " num_pq_pushed = " << num_pq_pushed << std::endl;
      }
      // Store the predicted errors into the buffer
      predicted_errors_buffer = node.errs;

      return;
    }

    if (node.num_dets > max_num_dets) continue;

    if (!discovered_dets[node.num_dets].insert(node.dets).second) {
      continue;
    }

    if (config.verbose) {
      std::cout.precision(13);
      std::cout << "len(pq) = " << pq.size() << " num_pq_pushed = " << num_pq_pushed << std::endl;
      std::cout << "num_dets = " << node.num_dets << " max_num_dets = " << max_num_dets
                << " cost = " << node.cost << std::endl;
      for (size_t oei : node.errs) {
        std::cout << oei << ", ";
      }
      std::cout << std::endl;
    }

    if (node.num_dets < min_num_dets) {
      min_num_dets = node.num_dets;
      for (size_t i = min_num_dets + det_beam + 1; i <= max_num_dets; ++i) {
        discovered_dets[i].clear();
      }
      max_num_dets = std::min(max_num_dets, min_num_dets + det_beam);
    }

    // Choose the min det to be the minimum index activated detector
    size_t min_det = std::numeric_limits<size_t>::max();
    for (size_t d = 0; d < num_detectors; ++d) {
      if (node.dets[config.det_orders[det_order][d]]) {
        min_det = config.det_orders[det_order][d];
        break;
      }
    }

    // Recompute the det counts
    std::vector<size_t> det_counts(num_errors, 0);
    for (size_t d = 0; d < num_detectors; ++d) {
      if (!node.dets[d]) continue;
      for (int ei : d2e[d]) {
        det_counts[ei]++;
      }
    }
    // We cache as we recompute the det costs
    std::vector<double> det_costs(num_detectors, -1);
    // for (size_t d = 0; d < num_detectors; ++d) {
    //   if (!node.dets[d]) continue;
    //   det_costs[d] = get_detcost(d, node.blocked_errs, det_counts, node.dets);
    // }
    std::vector<bool> next_blocked_errs = node.blocked_errs;
    if (config.at_most_two_errors_per_detector) {
      for (int ei : d2e[min_det]) {
        // Block all errors of this detector -- note this is an approximation where we insist at
        // most 2 errors are incident to any detector
        next_blocked_errs[ei] = true;
      }
    }

    std::vector<bool> next_next_blocked_errs;
    // Consider activating any error of the lowest index activated detector
    for (size_t ei : d2e[min_det]) {
      if (node.blocked_errs[ei]) {
        continue;
      }
      next_blocked_errs[ei] = true;

      std::vector<size_t> next_errs = node.errs;
      next_errs.push_back(ei);

      std::vector<size_t> next_det_counts = det_counts;
      std::vector<bool> next_dets = node.dets;
      double next_cost = node.cost + errors[ei].likelihood_cost;

      size_t next_num_dets = node.num_dets;
      next_next_blocked_errs = next_blocked_errs;
      for (int d : edets[ei]) {
        if (next_dets[d]) {
          next_dets[d] = false;
          --next_num_dets;
          for (int oei : d2e[d]) {
            next_det_counts[oei]--;
          }
          if (config.at_most_two_errors_per_detector) {
            for (size_t oei : d2e[d]) {
              next_next_blocked_errs[oei] = true;
            }
          }
        } else {
          next_dets[d] = true;
          ++next_num_dets;
          for (int oei : d2e[d]) {
            next_det_counts[oei]++;
          }
        }
      }

      if (next_num_dets > max_num_dets) continue;

      if (discovered_dets[next_num_dets].find(next_dets) != discovered_dets[next_num_dets].end()) {
        continue;
      }

      for (int d : edets[ei]) {
        if (node.dets[d]) {
          if (det_costs[d] == -1) {
            det_costs[d] = get_detcost(d, node.blocked_errs, det_counts, node.dets);
          }
          next_cost -= det_costs[d];
        } else {
          // TODO: bug next_next_blocked_errs should be used instead
          next_cost += get_detcost(d, next_next_blocked_errs, next_det_counts, next_dets);
        }
      }
      for (size_t od : eneighbors[ei]) {
        if (!node.dets[od] || !next_dets[od]) continue;
        if (det_costs[od] == -1) {
          det_costs[od] = get_detcost(od, node.blocked_errs, det_counts, node.dets);
        }
        next_cost -= det_costs[od];
        // TODO: bug next_next_blocked_errs should be used instead
        next_cost += get_detcost(od, next_next_blocked_errs, next_det_counts, next_dets);
      }

      if (next_cost == INF) {
        continue;
      }

      // pq.push({next_errs, next_dets, next_cost, next_num_dets, next_blocked_errs});
      pq.push({next_cost, next_num_dets, next_errs});
      ++num_pq_pushed;

      if (num_pq_pushed > config.pqlimit) {
        low_confidence_flag = true;
        return;
      }
    }
  }

  assert(pq.empty());
  if (config.verbose) {
    std::cout << "Decoding failed to converge within beam limit." << std::endl;
  }
  low_confidence_flag = true;
  return;
}

double TesseractDecoder::cost_from_errors(const std::vector<size_t>& predicted_errors) {
  double total_cost = 0;
  // Iterate over all errors and add to the mask
  for (size_t ei : predicted_errors_buffer) {
    total_cost += errors[ei].likelihood_cost;
  }
  return total_cost;
}

common::ObservablesMask TesseractDecoder::mask_from_errors(
    const std::vector<size_t>& predicted_errors) {
  common::ObservablesMask mask = 0;
  // Iterate over all errors and add to the mask
  for (size_t ei : predicted_errors_buffer) {
    mask ^= errors[ei].symptom.observables;
  }
  return mask;
}

common::ObservablesMask TesseractDecoder::decode(const std::vector<size_t>& detections) {
  decode_to_errors(detections);
  return mask_from_errors(predicted_errors_buffer);
}

void TesseractDecoder::decode_shots(
    std::vector<stim::SparseShot>& shots, std::vector<common::ObservablesMask>& obs_predicted) {
  obs_predicted.resize(shots.size());
  for (size_t i = 0; i < shots.size(); ++i) {
    obs_predicted[i] = decode(shots[i].hits);
  }
}
