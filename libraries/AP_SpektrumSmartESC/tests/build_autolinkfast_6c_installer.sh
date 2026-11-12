# This is a FAST-START TIMING EXPERIMENT ONLY; it does NOT send anything
# before ArduPilot begins. Motor phases DISCONNECTED, never arm/flight.
# Listen 50ms + prepare 50ms after app init; native 0x21-whitelist only.
#!/usr/bin/env bash
# COMPILE ONLY: explicit Pixhawk6C+MAIN3-inhibited IOMCU AutoLinkFast Lab image.
# Safe scope: 0x21 handshakes only. No 0xCD, throttle, reverse, or arming.
# Flash only after review; this .apj updates the companion IOMCU.
set -euo pipefail
cd "$(dirname "$0")/../../.."

python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py

io_board="iomcu-f103-spktsmart"
fmu_board="Pixhawk6C-SpktSmart-AutoLinkFast-IOInstall"
io="build/$io_board/bin/iofirmware_lowpolh.bin"
bin="build/$fmu_board/bin/arduplane"
apj="$bin.apj"

./waf configure --board "$io_board"
./waf iofirmware
test -s "$io"

# Match the installed STM32F103 special IO image using the exact
# firmware CRC algorithm (CRC32 init=0, no final XOR, FF pad to F000).
python3 - "$io" <<'PY'
from pathlib import Path
import sys
raw = Path(sys.argv[1]).read_bytes()
capacity = 0xF000
if not raw or len(raw) > capacity:
    raise SystemExit(f"Bad companion IO image size: {len(raw)}")
crc = 0
for b in raw + bytes([255]) * (capacity-len(raw)):
    crc ^= b
    for _ in range(8):
        crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
print(f"AutoLinkFast special IO image length={len(raw)} CRC=0x{crc:08X}")
if crc != 0x6DBBAD71:
    raise SystemExit("Companion IO CRC changed. Review image before installer build.")
PY

./waf configure --board "$fmu_board"
./waf plane
test -s "$bin"
test -s "$apj"

# Hard evidence the target has the *AUTO* link observer compiled in and
# does NOT include the TurnLab 0xCD control execution path.
strings "$bin" | grep -F "SPK17 AUTOLAB ON; NO MOTOR, NO CONTROL" > /dev/null
strings "$bin" | grep -F "SPK17 %s" > /dev/null
# This variant must include sub-second evidence from the vendor D00 / FC
# D40 collision investigation, not just the old 5-second SPK17 snapshots.
strings "$bin" | grep -F "SPK24 A%lu F%lu L%lu Q%lu R%lu" > /dev/null
strings "$bin" | grep -F "SPK24 ERR FE%lu NE%lu O%lu REL%lu" > /dev/null
strings "$bin" | grep -F "SPK24 TIMES RX%lu TC%lu" > /dev/null
strings "$bin" | grep -F "SPK25 FAST INIT%lu L%lu P%lu" > /dev/null
strings "$bin" | grep -F "SPK25 INIT%lu FIRSTTC%lu NOW%lu" > /dev/null
strings "$bin" | grep -F "SPK26 G%u RX%lu ERR%lu" > /dev/null
strings "$bin" | grep -F "SPKT MOTOR BLOCKED: SOURCE/IO/ESC UNQUALIFIED" > /dev/null
if strings "$bin" | grep -F "SPK22 ZERO TRIAL CH1=" > /dev/null; then
    echo "Refusing image with TurnLab 0xCD trial enabled." >&2
    exit 1
fi

python3 - "$apj" <<'PY'
from pathlib import Path
import json, sys
a = json.loads(Path(sys.argv[1]).read_text())
if int(a.get("board_id", -1)) != 56:
    raise SystemExit(f"Not a Pixhawk6C board_id=56: {a.get('board_id')}")
print("AutoLinkFast Pixhawk6C board ID=56")
PY

sha256sum "$io" "$apj"
printf '\nAUTOLINKFAST INSTALLER BUILD PASS: auto 0x21 handshake ONLY (no motor).\n'
printf 'BUILD ONLY; NOT FLASHED. This image attempts IOMCU update on boot.\n'
printf 'Firmware: %s\n' "$apj"
