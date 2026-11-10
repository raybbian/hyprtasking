#!/usr/bin/env python3
"""Exercise production handlers with substitutes for compositor IPC and rendering.

Run in a native build: python3 tests/handlers.py
Meson skips this fixture when cross compiling.
Requires a C++23 compiler, pkg-config and xkbcommon and hyprutils development files.
This does not load a plugin or verify rendering in a live Hyprland session.
"""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--standard", choices=("c++23", "c++2b"), default="c++23")
parser.add_argument("compiler", nargs=argparse.REMAINDER)
args = parser.parse_args()
compiler = args.compiler
if compiler and compiler[0] == "--":
    compiler = compiler[1:]
if not compiler:
    compiler = shlex.split(os.environ.get("CXX", "c++"))


def function(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


with tempfile.TemporaryDirectory(prefix="hyprtasking-tests-") as directory:
    source = Path(directory) / "handlers.cpp"
    source.write_text(
        (ROOT / "tests/handlers.inc").read_text()
        + "\n"
        + function("src/overview.cpp", "void HTView::show(")
        + "\n"
        + function("src/overview.cpp", "void HTView::hide(")
        + "\n"
        + function("src/input.cpp", "bool HTManager::on_key(")
        + "\n"
        + (ROOT / "tests/scenarios.inc").read_text()
    )
    flags = subprocess.check_output(
        ["pkg-config", "--cflags", "--libs", "xkbcommon", "hyprutils"], text=True
    )
    binary = Path(directory) / "handlers"
    subprocess.run(
        compiler
        + ["-std=" + args.standard, "-Wall", "-Wextra", "-Wno-unused-parameter", str(source), "-o", str(binary)]
        + shlex.split(flags),
        check=True,
    )
    result = subprocess.run([str(binary)])
    raise SystemExit(result.returncode if result.returncode >= 0 else 128 - result.returncode)
