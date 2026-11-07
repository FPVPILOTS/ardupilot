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
if strings "$bin" | grep -Fq "SPK17 AUTOLAB ON"; then
    printf '\nPHASE17C BUILD PASS (UNARMABLE LAB). Artifact: %s.apj\n' "$bin"
fi
printf 'DO NOT FLASH until peer-reviewed checks and physical bench setup are confirmed.\n'
