load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@rules_proto//proto:defs.bzl", "proto_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "storage_proto",
    srcs = ["storage.proto"],
)

cc_proto_library(
    name = "storage_cc_proto",
    deps = [":storage_proto"],
)

py_proto_library(
    name = "storage_py_proto",
    srcs = ["storage.proto"],
    srcs_version = "PY2AND3",
)

proto_library(
    name = "analysis_proto",
    srcs = ["analysis.proto"],
    deps = [
        ":storage_proto",
        # NOTE: These dependencies are not used in our local copy (which is
        # modified because of the py_proto_library limitation). Keep it here
        # until py_proto_library gets fixed.
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

cc_proto_library(
    name = "analysis_cc_proto",
    deps = [":analysis_proto"],
)

py_proto_library(
    name = "analysis_py_proto",
    srcs = ["analysis.proto"],
    srcs_version = "PY2AND3",
    deps = [
        ":storage_py_proto",
    ],
)
