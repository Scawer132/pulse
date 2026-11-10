#!/usr/bin/env python3
"""Reject new fixed-size arrays of 8 KB or more in the UI modules.

A 64 KB stack buffer is what turned a deep message chain into a stack overflow
(see docs/design/stack-safety-plan.md): every nested message already holds a whole
WndProcImpl frame, so a large local array is what finally pushes it over. The
existing buffers stay as they are; this check only looks at lines a change
adds, so old code needs no annotation.

    python tools/ci/check_stack_arrays.py --base origin/main   # pull request
    python tools/ci/check_stack_arrays.py --base HEAD~1        # push
    python tools/ci/check_stack_arrays.py --all                # one-off audit

A line that is not a stack array (a struct member, or a small buffer that is
deliberate) opts out with an inline `// stack-ok: <reason>`.
"""

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

DIRS = ("src/app", "src/ui", "src/fs", "src/ops")
SUFFIXES = (".cpp", ".h")
THRESHOLD_BYTES = 8192

ELEMENT_BYTES = {
    "char": 1,
    "char8_t": 1,
    "uint8_t": 1,
    "int8_t": 1,
    "std::byte": 1,
    "BYTE": 1,
    "wchar_t": 2,
    "char16_t": 2,
    "uint16_t": 2,
    "int16_t": 2,
    "short": 2,
    "char32_t": 4,
    "uint32_t": 4,
    "int32_t": 4,
    "int": 4,
    "unsigned": 4,
    "DWORD": 4,
    "UINT": 4,
}
_TYPE = r"(?:wchar_t|char8_t|char16_t|char32_t|char|BYTE|std::byte|uint8_t|int8_t|" \
        r"uint16_t|int16_t|uint32_t|int32_t|UINT|DWORD|int|short|unsigned)"
ARRAY = re.compile(rf"\b({_TYPE})\s+\w+\s*\[\s*(\d+)\s*\]")
STD_ARRAY = re.compile(r"std::array\s*<\s*([\w:]+)\s*,\s*(\d+)\s*>")
HUNK = re.compile(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@")


def array_bytes(line: str):
    """(bytes, description) for the first large array on `line`, else None."""
    for match in ARRAY.finditer(line):
        size = ELEMENT_BYTES.get(match.group(1))
        if size is None:
            continue
        total = size * int(match.group(2))
        if total >= THRESHOLD_BYTES:
            return total, f"{match.group(1)}[{match.group(2)}]"
    for match in STD_ARRAY.finditer(line):
        size = ELEMENT_BYTES.get(match.group(1))
        if size is None:
            continue
        total = size * int(match.group(2))
        if total >= THRESHOLD_BYTES:
            return total, f"std::array<{match.group(1)}, {match.group(2)}>"
    return None


def check_line(path: str, lineno: int, line: str) -> str | None:
    if "stack-ok" in line:
        return None
    stripped = line.lstrip()
    if stripped.startswith("//") or stripped.startswith("*"):
        return None
    found = array_bytes(line.split("//", 1)[0])
    if not found:
        return None
    total, what = found
    return f"{path}:{lineno}: {total} bytes ({what})"


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def resolve_base(explicit: str | None) -> str | None:
    candidates = []
    if explicit:
        candidates.append(explicit)
    base_ref = os.environ.get("GITHUB_BASE_REF")
    if base_ref:
        candidates.append(f"origin/{base_ref}")
    candidates.append("HEAD~1")
    for candidate in candidates:
        if not candidate or not candidate.strip("0"):
            continue
        if git("rev-parse", "--verify", f"{candidate}^{{commit}}").returncode == 0:
            return candidate
    return None


def added_lines(base: str) -> list[tuple[str, int, str]]:
    """(path, new line number, text) for every line a change adds."""
    result = git("diff", "--unified=0", "--no-color", "--diff-filter=AM",
                 f"{base}...HEAD", "--", *DIRS)
    if result.returncode != 0:
        raise SystemExit(f"git diff against {base} failed:\n{result.stderr.strip()}")
    lines = []
    path = None
    new_line = 0
    for raw in result.stdout.splitlines():
        if raw.startswith("+++ b/"):
            path = raw[6:]
            continue
        if raw.startswith("+++ ") or raw.startswith("--- "):
            path = None if raw.startswith("--- /dev/null") else path
            continue
        hunk = HUNK.match(raw)
        if hunk:
            new_line = int(hunk.group(1))
            continue
        if path is None:
            continue
        if raw.startswith("+"):
            lines.append((path, new_line, raw[1:]))
            new_line += 1
    return lines


def all_lines() -> list[tuple[str, int, str]]:
    lines = []
    for directory in DIRS:
        for path in sorted(Path(directory).rglob("*")):
            if path.suffix not in SUFFIXES or not path.is_file():
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for number, line in enumerate(text.splitlines(), start=1):
                lines.append((path.as_posix(), number, line))
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", help="revision the change is compared with")
    parser.add_argument("--all", action="store_true",
                        help="audit the whole tree instead of the diff")
    args = parser.parse_args()

    if args.all:
        lines = all_lines()
        scope = f"the whole tree ({len(lines)} lines)"
    else:
        base = resolve_base(args.base)
        if base is None:
            print("stack arrays: cannot resolve a base revision; pass --base <rev> "
                  "or use --all", file=sys.stderr)
            return 2
        lines = added_lines(base)
        scope = f"{len(lines)} added lines against {base}"

    violations = [entry for entry in
                  (check_line(path, number, text) for path, number, text in lines)
                  if entry]
    if not violations:
        print(f"stack arrays: clean ({scope})")
        return 0

    for violation in violations:
        print(violation, file=sys.stderr)
    print(f"\n{len(violations)} fixed-size array(s) of {THRESHOLD_BYTES} bytes or more "
          f"in {', '.join(DIRS)}.", file=sys.stderr)
    print("Move the buffer to the heap, shrink it, or mark the line with "
          "`// stack-ok: <reason>` when it is not a stack array.", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
