package(default_visibility = ["//iamf/cli/tests:__subpackages__"])

cc_test(
    name = "adm_to_user_metadata_main_lib_test",
    srcs = ["adm_to_user_metadata_main_lib_test.cc"],
    deps = [
        "//iamf/cli/adm_to_user_metadata/app:adm_to_user_metadata_main_lib",
        "//iamf/cli/proto:audio_frame_cc_proto",
        "//iamf/cli/proto:test_vector_metadata_cc_proto",
        "//iamf/cli/proto:user_metadata_cc_proto",
        "@com_google_absl//absl/base:no_destructor",
        "@com_google_absl//absl/strings:string_view",
        "@com_google_googletest//:gtest_main",
    ],
)
