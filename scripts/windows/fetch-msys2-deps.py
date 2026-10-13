#!/usr/bin/env python3
"""Download MSYS2 UCRT64 packages and their dependencies for cross-compiling
the Windows build on Linux, the same libraries upstream's CI installs.

Usage: scripts/windows/fetch-msys2-deps.py <dest> [package ...]
Unpacks into <dest>/ucrt64.  Needs curl, tar and zstd.
"""

import io
import os
import subprocess
import sys
import tarfile
import urllib.request

REPO = "https://repo.msys2.org/mingw/ucrt64"
PREFIX = "mingw-w64-ucrt-x86_64-"

# Same as .github/workflows/windows-x64-build.yml, minus the toolchain.
DEFAULT = ["allegro", "enet", "fluidsynth", "libmad"]

# Toolchain and headers come from llvm-mingw; runtime DLLs that
# GCC-built libraries need are kept (gcc-libs, libwinpthread).
SKIP = {"crt-git", "headers-git", "gcc", "binutils", "windows-default-manifest",
        "python", "tcl", "tk"}


def strip_version(dep):
    for op in (">=", "<=", "=", ">", "<"):
        if op in dep:
            return dep.split(op, 1)[0]
    return dep


def load_db(dest):
    db_path = os.path.join(dest, "ucrt64.db")
    if not os.path.exists(db_path):
        urllib.request.urlretrieve(REPO + "/ucrt64.db", db_path)

    raw = subprocess.run(["zstd", "-dc", db_path], check=True, capture_output=True).stdout
    pkgs, provides = {}, {}

    with tarfile.open(fileobj=io.BytesIO(raw)) as tar:
        for m in tar.getmembers():
            if not m.name.endswith("/desc"):
                continue
            fields, key = {}, None
            for line in tar.extractfile(m).read().decode().splitlines():
                if line.startswith("%") and line.endswith("%"):
                    key = line.strip("%")
                    fields[key] = []
                elif line and key:
                    fields[key].append(line)
            name = fields["NAME"][0]
            pkgs[name] = fields
            for p in fields.get("PROVIDES", []):
                provides[strip_version(p)] = name

    return pkgs, provides


def resolve(pkgs, provides, wanted):
    seen, order = set(), []

    def visit(name):
        name = strip_version(name)
        name = name if name in pkgs else provides.get(name, name)
        if name in seen or not name.startswith(PREFIX) or name[len(PREFIX):] in SKIP:
            return
        if name not in pkgs:
            print("warning: no package", name, file=sys.stderr)
            return
        seen.add(name)
        for dep in pkgs[name].get("DEPENDS", []):
            visit(dep)
        order.append(name)

    for w in wanted:
        visit(PREFIX + w)
    return order


def main():
    dest = os.path.abspath(sys.argv[1])
    wanted = sys.argv[2:] or DEFAULT
    os.makedirs(os.path.join(dest, "pkgs"), exist_ok=True)

    pkgs, provides = load_db(dest)
    for name in resolve(pkgs, provides, wanted):
        filename = pkgs[name]["FILENAME"][0]
        path = os.path.join(dest, "pkgs", filename)
        if not os.path.exists(path):
            print("fetch", filename)
            urllib.request.urlretrieve(REPO + "/" + filename, path)
        subprocess.run(["tar", "--zstd", "-xf", path, "-C", dest,
                        "--exclude=.BUILDINFO", "--exclude=.MTREE", "--exclude=.PKGINFO"],
                       check=True)

    print("unpacked into", os.path.join(dest, "ucrt64"))


if __name__ == "__main__":
    main()
