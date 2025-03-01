# Copyright 2025 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//common/benchmark:benchmark.bzl", "cc_benchmark")

package(default_visibility = ["//visibility:public"])

config_setting(
    name = "debug",
    values = {"compilation_mode": "dbg"},
)

OPT_COPTS = select({
    "//conditions:default": [
        "-Ofast",
        "-fno-fast-math",
        "-flto",
        "-march=native",
    ],
    ":debug": ["-Og"],
}) + ["-std=c++20"]

OPT_LINKOPTS = select({
    "//conditions:default": [
        "-Ofast",
        "-fno-fast-math",
        "-flto",
        "-march=native",
    ],
    ":debug": [],
}) + [
    "-lpthread",
]

cc_library(
    name = "libcommon",
    srcs = [
        "common.cc",
    ],
    hdrs = [
        "common.h",
    ],
    copts = OPT_COPTS,
    linkopts = OPT_LINKOPTS,
    deps = [
        "@stim//:stim_lib",
    ],
)

cc_library(
    name = "libutils",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    copts = OPT_COPTS,
    linkopts = OPT_LINKOPTS,
    visibility = ["//visibility:public"],
    deps = [
        ":libcommon",
        "@stim//:stim_lib",
    ],
)

cc_library(
    name = "libtesseract",
    srcs = ["tesseract.cc"],
    hdrs = ["tesseract.h"],
    copts = OPT_COPTS,
    linkopts = OPT_LINKOPTS,
    deps = [
        ":libutils",
    ],
)

cc_binary(
    name = "tesseract",
    srcs = ["tesseract_main.cc"],
    copts = OPT_COPTS,
    linkopts = OPT_LINKOPTS,
    deps = [
        ":libtesseract",
        "@argparse",
        "@nlohmann_json//:json",
        "@stim//:stim_lib",
    ],
)

cc_test(
    name = "tesseract_tests",
    timeout = "eternal",
    srcs = ["tesseract.test.cc"],
    tags = ["exclusive"],
    deps = [
        ":libsimplex",
        ":libtesseract",
        ":libutils",
        "@gtest",
        "@gtest//:gtest_main",
        "@rapidjson",
        "@stim//:stim_lib",
    ],
)

cc_test(
    name = "common_tests",
    srcs = ["common.test.cc"],
    deps = [
        ":libcommon",
        "@gtest",
        "@gtest//:gtest_main",
        "@stim//:stim_lib",
    ],
)

PERF_FILES = glob(["**/*.perf.*"])

cc_benchmark(
    name = "tesseract_perf",
    srcs = PERF_FILES,
    copts = OPT_COPTS,
    linkopts = OPT_LINKOPTS,
    deps = [
        ":libsimplex",
        ":libtesseract",
    ],
)

cc_library(
    name = "libsimplex",
    srcs = [
        "simplex.cc",
    ],
    hdrs = [
        "simplex.h",
    ],
    copts = OPT_COPTS,
    linkopts = OPT_LINKOPTS,
    deps = [
        ":libcommon",
        "@highs",
        "@stim//:stim_lib",
    ],
)

cc_binary(
    name = "simplex",
    srcs = [
        "simplex_main.cc",
    ],
    copts = OPT_COPTS,
    linkopts = OPT_LINKOPTS,
    deps = [
        ":libcommon",
        ":libsimplex",
        "@argparse",
        "@nlohmann_json//:json",
    ],
)
