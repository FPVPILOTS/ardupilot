#!/usr/bin/env bash
# Reproducible Pixhawk 6C Viper 90 mm ArduPlane release-candidate builder.
# Never flashes, arms, writes params or runs motors.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
test -d .git || { echo "Not in ArduPilot checkout" >&2; exit 1; }
command -v sha256sum >/dev/null
command -v tar >/dev/null
command -v c++ >/dev/null
command -v python3 >/dev/null

SHA="$(git rev-parse HEAD)"
BRANCH="$(git branch --show-current)"
if [[ "$BRANCH" != "release/viper90-hobbywing-v5-pixhawk6c-rc1" ]]; then
    echo "Unexpected branch: $BRANCH" >&2; exit 2
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "Tracked working tree has changes; commit/revert before release build" >&2; exit 2
fi

echo "== Host tests =="
bash Tools/hobbywing_v5/run_host_tests.sh

echo "== Build Pixhawk6C ArduPlane =="
./waf configure --board Pixhawk6C
./waf plane

FIRMWARE="$ROOT/build/Pixhawk6C/bin/arduplane.apj"
test -s "$FIRMWARE"
OUT="$ROOT/build/viper90-release/rc1-${SHA:0:12}"
mkdir -p "$OUT"
cp "$FIRMWARE" "$OUT/arduplane-Pixhawk6C-Viper90-rc1.apj"
{
    printf 'Project: Viper 90mm Hobbywing Platinum 150A V5.1 ArduPlane\n'
    printf 'Release class: RC1 - not flight validated by this script\n'
    printf 'Branch: %s\n' "$BRANCH"
    printf 'Commit: %s\n' "$SHA"
    printf 'UTC build: %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'Target: Pixhawk6C ArduPlane\n'
    printf 'Host tests: PASS\n'
    printf 'Waf build: PASS\n'
    printf 'Flight check: requires operator confirmation / logs\n'
} >"$OUT/RELEASE.txt"
if [[ $# -ge 1 ]]; then
    test -f "$1" || { echo "No .param file: $1" >&2; exit 2; }
    echo "== Inspect supplied aircraft parameters (read-only) =="
    python3 Tools/hobbywing_v5/audit_viper_plane.py "$1" --strict | tee "$OUT/PARAM_AUDIT.txt"
    cp "$1" "$OUT/viper-aircraft.param"
fi
(
    cd "$OUT"
    sha256sum ./*.apj > SHA256SUMS
    if [[ -f viper-aircraft.param ]]; then
        sha256sum viper-aircraft.param >> SHA256SUMS
    fi
)
echo "RELEASE CANDIDATE FILES: $OUT"
cat "$OUT/RELEASE.txt"
cat "$OUT/SHA256SUMS"
echo "Firmware is NOT automatically flashed; verify the boot version after flashing."
