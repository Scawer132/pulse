# Per-clone setup for the fork workflow (see local/merge-policy.md).
#
# Configures the l10n merge driver, rerere, zdiff3 conflict style and the
# pre-commit gate. Idempotent; run once per clone (and again after moving it):
#   pwsh -File tools/local/setup-git.ps1
$ErrorActionPreference = "Stop"

$root = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $root) { throw "not inside a git work tree" }
$root = $root.Trim()

foreach ($pair in @(
        @("rerere.enabled", "true"),
        @("merge.conflictstyle", "zdiff3"),
        @("merge.l10n.driver", "cp %B %A"))) {
    & git -C $root config --local $pair[0] $pair[1]
    if ($LASTEXITCODE -ne 0) { throw "git config $($pair[0]) failed" }
}

$hookRel = (& git -C $root rev-parse --git-path hooks/pre-commit).Trim()
$hookPath = if ([System.IO.Path]::IsPathRooted($hookRel)) { $hookRel } else { Join-Path $root $hookRel }

$hook = @'
#!/bin/sh
# Installed by tools/local/setup-git.ps1 (per clone; never tracked).
branch=$(git symbolic-ref --quiet --short HEAD 2>/dev/null || echo "")
if [ "$branch" = "main" ]; then
    echo "pre-commit: main is the read-only upstream mirror; commit on local-main instead." >&2
    exit 1
fi
if git diff --cached --name-only | grep -qE '^(tools/l10n/|src/common/(string_ids|localization)\.h$|src/common/strings(_zh_tw)?\.rcinc$|src/common/zh_hant_table\.inc$|src/setup/setup_strings\.generated\.inc$)'; then
    python tools/l10n/gen_strings.py --check || {
        echo "pre-commit: l10n outputs are stale; run: python tools/l10n/gen_strings.py" >&2
        exit 1
    }
fi
'@

# Git runs hooks with sh; CRLF line endings break the shebang, so force LF.
$hook = $hook -replace "`r`n", "`n"
[System.IO.File]::WriteAllText($hookPath, $hook, (New-Object System.Text.UTF8Encoding($false)))

Write-Host "configured rerere, zdiff3 and merge.l10n.driver"
Write-Host "installed $hookPath"
