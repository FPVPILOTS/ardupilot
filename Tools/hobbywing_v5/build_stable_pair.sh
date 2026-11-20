#!/usr/bin/env bash
# Official ArduPilot 4.7.3 source + optional Hobbywing telemetry.
# Emits two separate factory-default firmware binaries; never flashes/arms.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$PWD"
EXPECTED_BRANCH="stable/ardupilot-4.7.3-hobbywing-kst"
OFFICIAL_BASE="d9c5097c3d8333435712ffed86bca8ce00c4bb46"
[[ "$(git branch --show-current)" == "$EXPECTED_BRANCH" ]] || { echo "Wrong branch" >&2; exit 2; }
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || { echo "Tracked changes: commit or revert" >&2; exit 2; }
git merge-base --is-ancestor "$OFFICIAL_BASE" HEAD || { echo "Official stable 4.7.3 is not ancestor!" >&2; exit 2; }
SHA="$(git rev-parse HEAD)"
for v in ArduPlane ArduCopter; do
    grep -q '^#define FIRMWARE_VERSION 4,7,3,FIRMWARE_VERSION_TYPE_OFFICIAL$' "$v/version.h" || {
        echo "$v does not report official 4.7.3" >&2; exit 2;
    }
done

bash Tools/hobbywing_v5/run_host_tests.sh
for spec in "ArduPlane Pixhawk6C plane arduplane" "ArduCopter Pixhawk4 copter arducopter"; do
    read -r VEHICLE BOARD COMMAND BINARY <<< "$spec"
    echo "Building $VEHICLE 4.7.3 / $BOARD"
    ./waf configure --board "$BOARD"
    ./waf "$COMMAND"
    SRC="$ROOT/build/$BOARD/bin/$BINARY.apj"
    test -s "$SRC" || { echo "Missing built $SRC" >&2; exit 2; }
    OUT="$ROOT/build/firmware-family/stable-4.7.3-$SHA"
    mkdir -p "$OUT"
    NAME="${VEHICLE}-4.7.3-${BOARD}-Hobbywing-V5.1+KST-760us-560Hz"
    cp "$SRC" "$OUT/$NAME.apj"
    (cd "$OUT" && sha256sum "$NAME.apj" >> SHA256SUMS)
done
cat > "$OUT/BUILD_INFO.txt" <<EOF
Official upstream 4.7.3 base: $OFFICIAL_BASE
Custom source commit: $SHA
ArduPlane target: Pixhawk6C
ArduCopter target: Pixhawk4
Common firmware identity: ArduPilot 4.7.3 (official vehicle version metadata)
Module family label: Hobbywing V5.1 + KST 760us/560Hz
Hobbywing receive-only telemetry driver: integrated; default OFF
KST 760us/560Hz: NOT INTEGRATED IN THIS STABLE PORT YET
Vehicle parameter presets: NONE (stock firmware defaults)
Post-flash hardware verification: NOT PERFORMED BY SCRIPT
EOF
cat "$OUT/BUILD_INFO.txt"
echo "Pair available: $OUT"
