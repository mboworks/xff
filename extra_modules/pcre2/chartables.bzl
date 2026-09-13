# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Expose PCRE2's copied tables and upstream reference for materialization and drift tests."""

_ChartablesInfo = provider(
    doc = "Carries PCRE2's generated character tables and upstream reference through source dependencies.",
    fields = {"files": "Depset of generated character tables and upstream .dist reference artifacts."},
)

def _chartables_aspect_impl(target, ctx):
    direct = []
    transitive = []
    sources = getattr(ctx.rule.files, "src", [])
    if len(sources) == 1 and sources[0].basename == "pcre2_chartables.c.dist":
        direct = sources + target[DefaultInfo].files.to_list()
    for source in getattr(ctx.rule.attr, "srcs", []):
        if _ChartablesInfo in source:
            transitive.append(source[_ChartablesInfo].files)
    return [_ChartablesInfo(files = depset(direct, transitive = transitive))]

_chartables_aspect = aspect(
    implementation = _chartables_aspect_impl,
    attr_aspects = ["srcs"],
)

def _chartables_impl(ctx):
    files = ctx.attr.library[_ChartablesInfo].files.to_list()
    generated = [file for file in files if file.basename == "pcre2_chartables.c"]
    reference = [file for file in files if file.basename == "pcre2_chartables.c.dist"]
    if len(generated) != 1 or len(reference) != 1:
        fail("expected one PCRE2 chartables copy and one upstream .dist reference")
    return [
        DefaultInfo(files = depset(generated)),
        OutputGroupInfo(reference = depset(reference)),
    ]

chartables = rule(
    implementation = _chartables_impl,
    attrs = {"library": attr.label(mandatory = True, aspects = [_chartables_aspect])},
)
