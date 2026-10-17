# SPDX-FileCopyrightText: Copyright (c) M. Boerger, the MBO Works authors
# SPDX-License-Identifier: Apache-2.0

"""Materialize generated C-family inputs even when compiled outputs are cached."""

_CompileSourcesInfo = provider(
    doc = "Generated C-family files in each dependency's actual configuration.",
    fields = {"files": "Depset of generated source and header artifacts."},
)

_C_FAMILY_EXTENSIONS = [
    "c",
    "i",
    "h",
    "inl",
    "cc",
    "cpp",
    "cxx",
    "c++",
    "C",
    "CC",
    "cp",
    "CPP",
    "C++",
    "CXX",
    "ii",
    "ipp",
    "hh",
    "hpp",
    "hxx",
    "m",
    "mm",
    "M",
    "cu",
    "cui",
    "cl",
    "clcpp",
    "s",
    "asm",
    "S",
]

def _compile_sources_impl(target, ctx):
    direct = []
    if DefaultInfo in target:
        direct = [
            file
            for file in target[DefaultInfo].files.to_list()
            if not file.is_source and file.extension in _C_FAMILY_EXTENSIONS
        ]
    transitive = []
    for name in dir(ctx.rule.attr):
        value = getattr(ctx.rule.attr, name)
        if type(value) == "Target":
            dependencies = [value]
        elif type(value) == "list":
            dependencies = value
        elif type(value) == "dict":
            dependencies = value.keys() + value.values()
        else:
            continue
        for dependency in dependencies:
            if type(dependency) == "Target" and _CompileSourcesInfo in dependency:
                transitive.append(dependency[_CompileSourcesInfo].files)
    files = depset(direct, transitive = transitive)
    return [
        _CompileSourcesInfo(files = files),
        OutputGroupInfo(compile_sources = files),
    ]

compile_sources = aspect(
    implementation = _compile_sources_impl,
    # Follow source providers, tools, and transitions as well as library deps.
    attr_aspects = ["*"],
    apply_to_generating_rules = True,
)
