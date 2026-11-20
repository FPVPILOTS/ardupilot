#!/usr/bin/env bash
# Build both vehicles from one immutable ArduPilot revision with stock default params.
# No flashing, vehicle parameter writes, arming, or motor activation.
set -euo pipefail
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
BRANCH="integration/viper90-hobbywing-v5-kst"
[[ "$(git branch --show-current)" == "$BRANCH" ]] || { echo "Incorrect source branch" >&2; exit 2; }
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || { echo "Tracked sources modified" >&2; exit 2; }
SHA="$(git rev-parse HEAD)"
OUT="$ROOT/build/firmware-family/$SHA"
mkdir -p "$OUT"
bash Tools/hobbywing_v5/run_host_tests.sh

version_of() {
    local vehicle="$1" key="$2"
    awk -v k="$key" '$1=="#define" && $2==k {print $3; exit}' "$vehicle/version.h"
}
for spec in "ArduPlane:Pixhawk6C:plane:arduplane" "ArduCopter:Pixhawk4:copter:arducopter"; do
    IFS=: read -r VEHICLE BOARD WAF_TASK BINARY <<<"$spec"
    MAJOR="$(version_of "$VEHICLE" FW_MAJOR)"
    MINOR="$(version_of "$VEHICLE" FW_MINOR)"
    PATCH="$(version_of "$VEHICLE" FW_PATCH)"
    TYPE="$(version_of "$VEHICLE" FW_TYPE)"
    VERSION="$MAJOR.$MINOR.$PATCH"
    case "$TYPE" in
       FIRMWARE_VERSION_TYPE_DEV) VERSION="$VERSION-dev";;
       FIRMWARE_VERSION_TYPE_BETA) VERSION="$VERSION-beta";;
       FIRMWARE_VERSION_TYPE_OFFICIAL) ;;
       *) echo "Unknown version type: $TYPE" >&2; exit 2;;
    esac
    echo "=== $VEHICLE $VERSION / $BOARD ==="
    ./waf configure --board "$BOARD"
    ./waf "$WAF_TASK"
    ARTIFACT="$ROOT/build/$BOARD/bin/$BINARY.apj"
    [[ -s "$ARTIFACT" ]] || { echo "Missing APJ: $ARTIFACT" >&2; exit 2; }
    DEST="$OUT/$VEHICLE-$VERSION-$BOARD-Hobbywing-V5.1+KST-760us-560Hz.apj"
    cp "$ARTIFACT" "$DEST"
    sha256sum "$DEST" >> "$OUT/SHA256SUMS"
    printf '%s\n' "Vehicle: $VEHICLE" "Version: $VERSION" "Board: $BOARD" "Git SHA: $SHA" "Firmware output: $(basename "$DEST")" "Build: PASS" >> "$OUT/BUILD_INFO.txt"
    echo >> "$OUT/BUILD_INFO.txt"
done
cat > "$OUT/FEATURE_STATUS.txt" <<'EOF'
Hobbywing V5.1 decoder: included, default disabled until SERVO_BLH_HWV5=1.
KST 760us/560Hz: not yet integrated in this source branch.
The shared family label is a roadmap/product name, NOT proof KST runtime support.
Both vehicles use unmodified ArduPilot factory parameter defaults.
No airframe .param preset is embedded. No installed hardware test is implied.
ArduPlane: Pixhawk6C Viper; ArduCopter: Pixhawk4 Blade 500.
EOF
echo "Family build outputs: $OUT"
cat "$OUT/BUILD_INFO.txt"
cat "$OUT/FEATURE_STATUS.txt"
