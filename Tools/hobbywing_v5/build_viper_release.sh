#!/usr/bin/env bash
# Viper 90mm Hobbywing V5.1 — version-correct ArduPlane firmware packaging.
# Never flashes, arms, writes parameters, or activates motor outputs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
test -d .git || { echo "ArduPilot git checkout not found" >&2; exit 2; }

SHA="$(git rev-parse HEAD)"
BRANCH="$(git branch --show-current)"
if [[ "$BRANCH" != "integration/viper90-hobbywing-v5-kst" ]]; then
    echo "Wrong branch: $BRANCH" >&2; exit 2
fi
if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "Tracked source tree is modified. Commit or revert before packaging." >&2
    exit 2
fi

# NEVER invent a stable version: this file is authoritative for the
# APJ identity, Mission Planner version and firmware's own boot message.
VERSION_HEADER="$ROOT/ArduPlane/version.h"
get_define() { awk -v k="$1" '$1=="#define" && $2==k {print $3; exit}' "$VERSION_HEADER"; }
MAJOR="$(get_define FW_MAJOR)"
MINOR="$(get_define FW_MINOR)"
PATCH="$(get_define FW_PATCH)"
TYPE="$(get_define FW_TYPE)"
[[ "$MAJOR" =~ ^[0-9]+$ && "$MINOR" =~ ^[0-9]+$ && "$PATCH" =~ ^[0-9]+$ ]] || {
    echo "Cannot read ArduPlane version.h" >&2; exit 2;
}
VERSION="$MAJOR.$MINOR.$PATCH"
if [[ "$TYPE" == "FIRMWARE_VERSION_TYPE_DEV" ]]; then
    VERSION="$VERSION-dev"
elif [[ "$TYPE" == "FIRMWARE_VERSION_TYPE_BETA" ]]; then
    VERSION="$VERSION-beta"
elif [[ "$TYPE" != "FIRMWARE_VERSION_TYPE_OFFICIAL" ]]; then
    echo "Unrecognized firmware release type: $TYPE" >&2; exit 2
fi
NAME="ArduPlane-${VERSION}_Pixhawk6C_Hobbywing-V5.1+KST-760us-560Hz"

echo "== Hobbywing host verification =="
bash Tools/hobbywing_v5/run_host_tests.sh
echo "== Pixhawk6C ArduPlane firmware build =="
./waf configure --board Pixhawk6C
./waf plane
FW="$ROOT/build/Pixhawk6C/bin/arduplane.apj"
test -s "$FW" || { echo "Missing compiled .apj" >&2; exit 2; }
OUT="$ROOT/build/viper90-release/${NAME}-${SHA:0:12}"
mkdir -p "$OUT"
cp "$FW" "$OUT/${NAME}.apj"
{
    echo "Firmware identity: ArduPlane $VERSION"
    echo "Build target: Pixhawk6C"
    echo "Module family: Hobbywing V5.1 + KST 760us/560Hz"\n    echo "Built functionality: Hobbywing V5 telemetry; KST implementation not yet ported into this branch"
    echo "Vehicle: E-flite Viper 90mm"
    echo "Firmware branch: $BRANCH"
    echo "Exact commit: $SHA"
    echo "Firmware type: $TYPE"
    echo "Build UTC: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "Host test status: PASS"
    echo "Waf build status: PASS"
    echo "Hardware flash/regression: NOT PERFORMED by script"
} > "$OUT/BUILD_INFO.txt"

if [[ $# -ge 1 ]]; then
    test -f "$1" || { echo "Parameter file unavailable: $1" >&2; exit 2; }
    python3 Tools/hobbywing_v5/audit_viper_plane.py "$1" --strict | tee "$OUT/PARAM_AUDIT.txt"
    cp "$1" "$OUT/viper-aircraft.param"
fi
(
 cd "$OUT"
 sha256sum ./*.apj > SHA256SUMS
 if [[ -f viper-aircraft.param ]]; then sha256sum viper-aircraft.param >> SHA256SUMS; fi
)
echo "Package generated: $OUT"
cat "$OUT/BUILD_INFO.txt"
echo "Note: the stable release name 4.7.3 cannot be applied until this driver is actually ported to Plane-4.7.3 source."
