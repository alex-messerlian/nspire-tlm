#!/usr/bin/env python3
"""PERMANENT GATE: no tracked symlink may point inside the repository.

WHY, and it is a destroyed-data incident rather than a hypothetical.

corpus/raw is 5.1 GB of OpenStax source, untracked bulk data. To let train/prepare.py reach it from
a git WORKTREE I made corpus/raw a symlink to the main checkout's copy. It was untracked but not
ignored, so `git add -A` committed the symlink; the next `git merge --ff-only` into the main
checkout applied that commit and REPLACED the real directory with the link -- which then pointed at
its own path. 5.1 GB gone, no Time Machine destination configured, no APFS snapshot, empty trash.

THE LOAD-BEARING FACT IS NOT ABOUT `git add`. It is that a worktree shares its object store with the
main checkout, so ANYTHING COMMITTED IN A WORKTREE IS A PENDING EDIT TO THE OTHER TREE. A symlink
created for local convenience is therefore not local: on merge it is applied over whatever occupies
that path elsewhere. Convenience inside a worktree is a live edit to its sibling.

WHAT IT CHECKS: every tracked symlink, and whether its target resolves inside the repository. A
symlink pointing OUT of the repo is fine -- it cannot overwrite repo content. One pointing IN can
be checked out over a real directory in another worktree.
"""
import os, pathlib, subprocess, sys

ROOT = pathlib.Path(__file__).resolve().parents[2]


def main():
    out = subprocess.run(["git", "ls-files", "-s"], cwd=ROOT, capture_output=True, text=True).stdout
    if not out.strip():
        print("  CANNOT CHECK: git ls-files returned nothing. Refusing to report a pass.")
        return 2
    links, bad = [], []
    for line in out.splitlines():
        mode, _, rest = line.partition(" ")
        if mode != "120000":                    # git's mode for a symlink
            continue
        path = rest.split("\t", 1)[1] if "\t" in rest else rest
        links.append(path)
        p = ROOT / path
        try:    target = os.readlink(p)
        except OSError:  continue
        resolved = (p.parent / target).resolve() if not os.path.isabs(target) else pathlib.Path(target).resolve()
        try:
            resolved.relative_to(ROOT.resolve())
            bad.append((path, target))
        except ValueError:
            pass                                # points outside the repo: cannot clobber repo content
    print(f"  tracked symlinks: {len(links)}")
    for path, target in bad:
        print(f"  INSIDE-REPO SYMLINK  {path} -> {target}")
    if bad:
        print("\n  FAIL: a tracked symlink resolves inside the repository. On merge into another")
        print("  worktree this is checked out OVER whatever occupies that path -- which destroyed")
        print("  5.1 GB of corpus/raw. Ignore the path instead, or point the link outside the repo.")
        return 1
    print("  PASS: no tracked symlink points inside the repository")
    return 0


if __name__ == "__main__":
    sys.exit(main())
