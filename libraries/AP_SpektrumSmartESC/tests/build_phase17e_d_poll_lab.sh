#!/usr/bin/env bash
# A separately compiled bench-only one-shot 0xCD telemetry request image.
# NEVER flash or power a motor-connected ESC with this experimental build.
set -euo pipefail
cd "$(dirname "$0")/../../.."
python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py
./waf configure --board Pixhawk6C-SpktSmart-PollLab
./waf plane
bin="build/Pixhawk6C-SpktSmart-PollLab/bin/arduplane"
test -s "$bin"
test -s "${bin}.apj"
strings "$bin" | grep -F "SPK20 POLL TX" >/dev/null
strings "$bin" | grep -F "SPK20 POLLLAB: MOTOR PHASES MUST BE UNPLUGGED" >/dev/null
strings "$bin" | grep -F "SPK17 FAST OK" >/dev/null
printf '\nPHASE17E-D BUILD PASS (EXPERIMENTAL POLLLAB; NOT FOR FLIGHT)\n'
printf 'Artifact: %s.apj\n' "$bin"
printf 'DO NOT FLASH without motor phase wires physically unplugged, separate bench review and explicit go decision.\n'
