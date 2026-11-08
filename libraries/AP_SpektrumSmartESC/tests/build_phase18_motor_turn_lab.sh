#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py
./waf configure --board Pixhawk6C-SpktSmart-TurnLab
./waf plane
bin="build/Pixhawk6C-SpktSmart-TurnLab/bin/arduplane"
test -s "$bin"
test -s "${bin}.apj"
strings "$bin" | grep -F "SPK22 TURNLAB; UNARMABLE; PROFILE" >/dev/null
strings "$bin" | grep -F "SPK22 TC S%u CH1=%u CH7=%u RX%u" >/dev/null
strings "$bin" | grep -F "SPK17 FAST OK" >/dev/null
sha256sum "${bin}.apj"
printf '\nPHASE18 BUILD PASS: TurnLab only; source profile UNQUALIFIED, NO MOTOR GO.\n'
