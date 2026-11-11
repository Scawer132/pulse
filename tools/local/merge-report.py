#!/usr/bin/env python3
"""Preview the next upstream merge before touching the work tree.

    python tools/local/merge-report.py                 # local-main <- main
    python tools/local/merge-report.py --theirs main

Reports the merge base, what each side changed, the files that would conflict,
and the patch surface (how many upstream files the local branch edits). Uses
`git merge-tree --write-tree`, so it honours the l10n merge driver from
.gitattributes and writes nothing.
"""
from __future__ import annotations

import argparse
import subprocess
import sys

BASE_BRANCH = "local-main"
THEIRS_BRANCH = "main"


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def check(*args: str) -> str:
    proc = git(*args)
    if proc.returncode != 0:
        sys.exit(f"git {' '.join(args)} failed:\n{proc.stderr.strip()}")
    return proc.stdout


def names(args: list[str]) -> set[str]:
    return {line for line in check("diff", "--name-only", *args).splitlines() if line}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ours", default=BASE_BRANCH)
    parser.add_argument("--theirs", default=THEIRS_BRANCH)
    args = parser.parse_args()

    base = check("merge-base", args.ours, args.theirs).strip()
    upstream = names([f"{base}..{args.theirs}"])
    local = names([f"{base}..{args.ours}"])
    both = sorted(upstream & local)

    preview = git("merge-tree", "--write-tree", "--name-only", args.ours, args.theirs)
    lines = preview.stdout.splitlines()
    conflicted = [line for line in lines[1:] if line and not line.startswith(("Auto-merging", "CONFLICT", "changed in both"))]

    print(f"merge base : {base[:12]}  ({check('log', '-1', '--format=%s', base).strip()})")
    print(f"upstream   : {args.theirs} -> {len(upstream)} files {check('diff', '--shortstat', f'{base}..{args.theirs}').strip()}")
    print(f"local      : {args.ours} -> {len(local)} files {check('diff', '--shortstat', f'{base}..{args.ours}').strip()}")
    print(f"both sides : {len(both)} files")
    for name in both:
        # The same edit on both sides (e.g. a catalog row upstream also added)
        # merges cleanly; only differing content is worth a look.
        if check("rev-parse", f"{args.ours}:{name}") == check("rev-parse", f"{args.theirs}:{name}"):
            print(f"  {name}  (identical on both sides)")
            continue
        churn = check("diff", "--numstat", f"{base}..{args.ours}", "--", name).split("\t")[:2]
        print(f"  {name}  (local +{churn[0]}/-{churn[1]})")

    if preview.returncode == 0:
        print("conflicts  : none")
        return 0

    print(f"conflicts  : {len(conflicted)}")
    for name in conflicted:
        print(f"  {name}")
    print("\nResolve on local-main, then: python tools/l10n/gen_strings.py")
    return 1


if __name__ == "__main__":
    sys.exit(main())
