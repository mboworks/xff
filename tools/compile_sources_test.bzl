# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Analysis coverage for generated inputs across source providers and configurations."""

load("@bazel_skylib//lib:unittest.bzl", "analysistest", "asserts")
load(":compile_sources.bzl", "compile_sources")

def _fixture_sources_impl(ctx):
    files = []
    for extension in ["c", "cc", "h", "txt"]:
        file = ctx.actions.declare_file(ctx.label.name + "." + extension)
        ctx.actions.write(file, "")
        files.append(file)
    return [DefaultInfo(files = depset(files))]

_fixture_sources = rule(implementation = _fixture_sources_impl)

def _other_mode_impl(settings, _attr):
    mode = "fastbuild" if settings["//command_line_option:compilation_mode"] == "opt" else "opt"
    return {"//command_line_option:compilation_mode": mode}

_other_mode = transition(
    implementation = _other_mode_impl,
    inputs = ["//command_line_option:compilation_mode"],
    outputs = ["//command_line_option:compilation_mode"],
)

def _consumer_impl(_ctx):
    return []

_consumer = rule(
    implementation = _consumer_impl,
    attrs = {
        "deps": attr.label_list(),
        "tools": attr.label_list(cfg = "exec"),
        "transitioned": attr.label_list(cfg = _other_mode),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)

def _compile_sources_test_impl(ctx):
    env = analysistest.begin(ctx)
    target = analysistest.target_under_test(env)
    files = target[OutputGroupInfo].compile_sources.to_list()

    # Retain the C, C++, and header artifacts in all three configurations. Neither
    # the generated text file nor the handwritten source belongs in this group.
    asserts.equals(env, 9, len(files))
    asserts.equals(env, ["c"] * 3 + ["cc"] * 3 + ["h"] * 3, sorted([file.extension for file in files]))
    asserts.equals(env, 3, len({file.root.path: True for file in files}))
    return analysistest.end(env)

_compile_sources_test = analysistest.make(
    _compile_sources_test_impl,
    # Keep all three fixture configurations distinct even when CI runs optimized tests.
    config_settings = {"//command_line_option:compilation_mode": "fastbuild"},
    extra_target_under_test_aspects = [compile_sources],
)

def compile_sources_tests(name):
    """Exercise indirect source providers, exec edges, and a Starlark transition.

    Args:
      name: Name of the analysis test and prefix for its private fixtures.
    """
    _fixture_sources(name = name + "_fixture", tags = ["manual"])
    native.filegroup(
        name = name + "_fixture_group",
        srcs = [":" + name + "_fixture", "show_compiler.cc"],
        tags = ["manual"],
    )
    native.alias(
        name = name + "_fixture_alias",
        actual = ":" + name + "_fixture_group",
        tags = ["manual"],
    )
    _consumer(
        name = name + "_consumer",
        deps = [":" + name + "_fixture_alias"],
        tools = [":" + name + "_fixture_alias"],
        transitioned = [":" + name + "_fixture_alias"],
        tags = ["manual"],
    )
    _compile_sources_test(
        name = name,
        target_under_test = ":" + name + "_consumer",
    )
