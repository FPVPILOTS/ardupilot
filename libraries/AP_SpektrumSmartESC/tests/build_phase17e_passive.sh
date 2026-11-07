#!/usr/bin/env bash
# Phase 17E-A — complete host/build check; does NOT flash any hardware.
set -euo pipefail
cd "$(dirname "$0")/../../.."

bash libraries/AP_SpektrumSmartESC/tests/build_phase17c_autolab.sh

bin="build/Pixhawk6C-SpktSmart-AutoLab/bin/arduplane"
test -s "$bin"
test -s "${bin}.apj"
strings "$bin" | grep -F "SPK18 FRM H" >/dev/null
strings "$bin" | grep -F "SPK18 ESC CAND" >/dev/null
strings "$bin" | grep -F "SPK18 LAST D" >/dev/null

printf '\nPHASE17E-A PASS (PASSIVE 0x80 OBSERVER; UNARMABLE AUTOLAB)\n'
printf 'Artifact: %s.apj\n' "$bin"
printf 'WARNING: No 0xCD polling, no throttle, no reverse, no telemetry publication.\n'
printf 'DO NOT FLASH without separate hardware bench review.\n'
