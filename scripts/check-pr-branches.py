#!/usr/bin/env python3
"""Check that dos-port contains every pull request branch's changes.

Each pr/* branch holds one fix or feature on upstream master; the same
change should also be in dos-port, so that our builds get it before
upstream merges it. For every branch, this takes the diff from its
merge base with upstream master and looks for each run of added lines,
as consecutive lines, in the same file of the target. Leading and
trailing whitespace is ignored. A block that only matches once C
comment lines are left out is reported as differing in comments only
(dos-port's comments sometimes mention MS-DOS). Removed lines are not
checked.

Usage: scripts/check-pr-branches.py [--target REF] [--upstream REF]
       [--remote NAME] [-v] [branch ...]

Branches default to every pr/* branch on the remote (default: fork).
The target defaults to the working tree (so uncommitted edits count);
--target dos-port checks a commit instead. Exits 1 if anything is
missing.
"""

import argparse
import subprocess
import sys


def git(*args):
    return subprocess.run(("git",) + args, check=True, capture_output=True,
                          text=True, errors="replace").stdout


def added_blocks(diff):
    """Yield (path, [lines]) for each run of added lines in a diff."""
    path, block = None, []
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[6:] if line.startswith("+++ b/") else None
            continue
        if line.startswith("+") and path:
            block.append(line[1:].strip())
            continue
        if block:
            yield path, block
            block = []
    if block:
        yield path, block


def contains(lines, block):
    block = [b for b in block if b]
    if not block:
        return True
    n = len(block)
    stripped = [l.strip() for l in lines if l.strip()]
    return any(stripped[i:i + n] == block for i in range(len(stripped) - n + 1))


def is_comment(line):
    return line.startswith(("/*", "*", "//"))


def without_comments(lines):
    return [l for l in lines if not is_comment(l.strip())]


def read_target(target, path, cache):
    if path not in cache:
        try:
            if target is None:
                with open(path, encoding="utf-8", errors="replace") as f:
                    cache[path] = f.read().splitlines()
            else:
                cache[path] = git("show", f"{target}:{path}").splitlines()
        except (OSError, subprocess.CalledProcessError):
            cache[path] = None
    return cache[path]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--target", help="ref to check (default: working tree)")
    ap.add_argument("--upstream", default="upstream/master")
    ap.add_argument("--remote", default="fork")
    ap.add_argument("-v", "--verbose", action="store_true",
                    help="print every missing block in full")
    ap.add_argument("branches", nargs="*")
    args = ap.parse_args()

    branches = args.branches or [
        r.strip() for r in git("for-each-ref", "--format=%(refname:short)",
                               f"refs/remotes/{args.remote}/pr/").splitlines()]
    cache = {}
    missing_any = False
    for branch in branches:
        base = git("merge-base", branch, args.upstream).strip()
        diff = git("diff", "--no-color", "--no-renames", base, branch)
        total, missing, comments = 0, [], 0
        for path, block in added_blocks(diff):
            if not any(block):
                continue
            total += 1
            lines = read_target(args.target, path, cache)
            if lines is not None and contains(lines, block):
                continue
            if lines is not None and contains(without_comments(lines), without_comments(block)):
                comments += 1
                continue
            missing.append((path, block))
        status = "ok" if not missing else f"{len(missing)} of {total} blocks missing"
        if comments:
            status += f" ({comments} differ in comments only)"
        print(f"{branch}: {status}")
        for path, block in missing:
            shown = block if args.verbose else [block[0]] + (["..."] if len(block) > 1 else [])
            print(f"    {path}:")
            for line in shown:
                print(f"        {line}")
        missing_any |= bool(missing)
    return 1 if missing_any else 0


if __name__ == "__main__":
    sys.exit(main())
