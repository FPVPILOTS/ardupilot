#!/usr/bin/env bash
# Phase 17E-C — read-only dry-run, no new physical serial control packets.
set -euo pipefail
cd "$(dirname "$0")/../../.."

bash libraries/AP_SpektrumSmartESC/tests/build_phase17e_passive.sh

bin="build/Pixhawk6C-SpktSmart-AutoLab/bin/arduplane"
test -s "$bin"
test -s "${bin}.apj"
strings "$bin" | grep -F "SPK19 DRY R" >/dev/null
strings "$bin" | grep -F "SPK19 NO TX; 0xCD PREVIEW ONLY" >/dev/null

printf '\nPHASE17E-C PASS (READ-ONLY POLL PREVIEW; UNARMABLE AUTOLAB)\n'
printf 'Artifact: %s.apj\n' "$bin"
printf 'NO 0xCD UART TX; no motor/actuator operation; no real telemetry polling.\n'
printf 'DO NOT FLASH before physical bench review.\n'
