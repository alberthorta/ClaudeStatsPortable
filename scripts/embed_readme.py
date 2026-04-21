#!/usr/bin/env python3
"""PlatformIO pre-build: embeds README.md into src/readme_embed.h as a
PROGMEM C string so webconfig.cpp can serve it over HTTP without a filesystem.

Regenerates the header only when the source README changes, so successive
builds without README edits don't force a recompile of webconfig.cpp.
"""
import os

Import("env")  # provided by PlatformIO pre-build context  # noqa: F821

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
SRC = os.path.join(PROJECT_DIR, "README.md")
DST = os.path.join(PROJECT_DIR, "src", "readme_embed.h")

DELIM = "CLSTATS_README"


def build_header() -> str:
    with open(SRC, "rb") as f:
        content = f.read().decode("utf-8", errors="replace")
    if f")" + DELIM + '"' in content:
        raise RuntimeError(
            "README.md contains the raw-string delimiter used by embed_readme.py; "
            "bump DELIM in scripts/embed_readme.py."
        )
    return (
        "// Auto-generated from README.md at build time. Do not edit.\n"
        "#pragma once\n\n"
        "#include <pgmspace.h>\n\n"
        f"static const char README_MD[] PROGMEM = R\"{DELIM}(\n"
        f"{content}\n)"
        f"{DELIM}\";\n"
    )


def main():
    new_content = build_header()
    if os.path.exists(DST):
        with open(DST, "r") as f:
            if f.read() == new_content:
                return
    with open(DST, "w") as f:
        f.write(new_content)


main()
