#!/usr/bin/env bash
# Phase 17C: compile and inspect only. NEVER flash automatically.
set -euo pipefail
cd "$(dirname "$0")/../../.."
python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py
./waf configure --board Pixhawk6C-SpktSmart-AutoLab
./waf plane
bin="build/Pixhawk6C-SpktSmart-AutoLab/bin/arduplane"
test -s "$bin"
test -s "${bin}.apj"
strings "$bin" | grep -F "SPK17 AUTOLAB ON" >/dev/null
strings "$bin" | grep -F "SPK17 WHY" >/dev/null
strings "$bin" | grep -F "SPK17 UART Q" >/dev/null
strings "$bin" | grep -F "SPK17 HS TX" >/dev/null
strings "$bin" | grep -F "SPK17 PEER S" >/dev/null
# Phase 17D early announce must also appear in the built image.
strings "$bin" | grep -F "SPK17 FAST OK" >/dev/null
# The two preceding string scans already validated the firmware's
# AutoLab markers. Do not repeat with grep -q under pipefail: strings may
# receive SIGPIPE and silently skip the success banner despite exit code 0.
printf '\nPHASE17C BUILD PASS (UNARMABLE LAB). Artifact: %s.apj\n' "$bin"
printf 'DO NOT FLASH until peer-reviewed checks and physical bench setup are confirmed.\n'
