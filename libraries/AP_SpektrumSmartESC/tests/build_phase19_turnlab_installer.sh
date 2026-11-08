#!/usr/bin/env bash
# Explicitly requested Phase19 IO installer image. BUILD ONLY; no flash.
# WARNING: loading the resulting .apj onto a Pixhawk6C via Mission Planner
# WILL attempt an IOMCU firmware update on its next startup.
set -euo pipefail
cd "$(dirname "$0")/../../.."

# Validate native packet generation and all existing safety-contract tests
# before producing any operator-loadable firmware image.
python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py

io_board="iomcu-f103-spktsmart"
fmu_board="Pixhawk6C-SpktSmart-TurnLab-IOInstall"
io="build/$io_board/bin/iofirmware_lowpolh.bin"
bin="build/$fmu_board/bin/arduplane"
apj="${bin}.apj"

./waf configure --board "$io_board"
./waf iofirmware
test -s "$io"

# Reproduce AP_IOMCU::check_crc() from the exact freshly built IO bytes:
# crc32_small starts at 0 (NO initial/final XOR) and pads to 0xF000 with FF.
python3 - "$io" <<'PY'
from pathlib import Path
import sys
path = Path(sys.argv[1])
image = path.read_bytes()
capacity = 0xF000
if not image or len(image) > capacity:
    raise SystemExit(f"Invalid companion IO image length: {len(image)}")
crc = 0
for byte in image + b"\xff" * (capacity - len(image)):
    crc ^= byte
    for _ in range(8):
        crc = (crc >> 1) ^ (0xEDB88320 if crc & 1 else 0)
print(f"Special IO build: {len(image)} bytes, expected installed CRC 0x{crc:08X}")
print("CRC is an IDENTIFIER, not evidence the IO was installed or MAIN3 inhibited.")
PY

# This opt-in FMU target replaces the stock ROMFS io_firmware.bin entry
# with the above custom lowpolh image and enables IO update on its boot.
./waf configure --board "$fmu_board"
./waf plane
test -s "$bin"
test -s "$apj"
strings "$bin" | grep -F "SPK22 TURNLAB; UNARMABLE; PROFILE" >/dev/null
strings "$bin" | grep -F "SPK22 TC S%u CH1=%u CH7=%u RX%u" >/dev/null
strings "$bin" | grep -F "SPK22 IO INSTALLED CRC 0x%08lX" >/dev/null
strings "$bin" | grep -F "SPK22 ZERO TRIAL CH1=%u CH7 OMITTED; NO MOTION TX" >/dev/null
strings "$bin" | grep -F "SPK22 SAFE D%u STOP%u IO%u PEER%u UART%u" >/dev/null
strings "$bin" | grep -F "SPK22 AGE P%lu IO%lu ERR%lu HF%lu" >/dev/null
strings "$bin" | grep -F "SPK22 ESC CAND VcV%u eRPM%lu THd10%u M%04X" >/dev/null
strings "$bin" | grep -F "SPKRC S3 PWM%u MAP%u ARM%u RC%u FS%u" >/dev/null
strings "$bin" | grep -F "SPKRC INT V%u MAG%u REV%u STOP%u" >/dev/null

python3 - "$apj" <<'PY'
import json
from pathlib import Path
import sys
obj = json.loads(Path(sys.argv[1]).read_text())
if int(obj["board_id"]) != 56:
    raise SystemExit(f"Not Pixhawk6C board type 56: {obj['board_id']}")
print(f"Pixhawk6C board ID: {obj['board_id']}")
PY

sha256sum "$io" "$apj"
printf '\nPHASE19 INSTALLER BUILD PASS: opt-in Pixhawk6C FMU bundle with special IO.\n'
printf 'BUILD ONLY: no flash performed. Flashing this .apj WILL attempt to update IOMCU.\n'
printf 'TurnLab motor control remains blocked until physical ESC profile is qualified.\n'
printf 'Installer image: %s\n' "$apj"
