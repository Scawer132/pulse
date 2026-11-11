# Pre-merge / pre-push checks for the fork workflow (see local/merge-policy.md).
#
#   pwsh -File tools/local/verify.ps1              # full: l10n, maps, build, tests
#   pwsh -File tools/local/verify.ps1 -SkipBuild   # l10n and maps only
#   pwsh -File tools/local/verify.ps1 -SkipTests   # build without controller tests
#
# Stop the running build\pulse.exe before a full run: Windows locks the exe and
# the final link fails with LNK1104.
param(
    [switch]$SkipBuild,
    [switch]$SkipTests
)
$ErrorActionPreference = "Stop"

$root = (& git rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -ne 0 -or -not $root) { throw "not inside a git work tree" }
Set-Location $root

function Fail($message) {
    Write-Host "FAIL: $message" -ForegroundColor Red
    exit 1
}

Write-Host "== l10n outputs =="
python tools/l10n/gen_strings.py --check
if ($LASTEXITCODE -ne 0) { Fail "l10n outputs are stale; run python tools/l10n/gen_strings.py" }

Write-Host "== catalog.tsv tracks upstream =="
& git diff --quiet main -- tools/l10n/catalog.tsv
if ($LASTEXITCODE -ne 0) {
    Fail "tools/l10n/catalog.tsv differs from main; local strings belong in tools/l10n/local/catalog.tsv"
}

Write-Host "== module map =="
python tools/ci/gen_module_map.py --check --strict
if ($LASTEXITCODE -ne 0) { Fail "module map is stale; run python tools/ci/gen_module_map.py --write" }

if (-not $SkipBuild) {
    Write-Host "== build pulse =="
    & cmd /c "call scripts\vcvars.bat >nul && cmake --build build --target pulse"
    if ($LASTEXITCODE -ne 0) { Fail "build failed" }
}

if (-not $SkipBuild -and -not $SkipTests) {
    Write-Host "== app controller tests =="
    & build\pulse_app_controllers_test.exe
    if ($LASTEXITCODE -ne 0) { Fail "pulse_app_controllers_test failed" }
}

Write-Host "verify: PASS"
