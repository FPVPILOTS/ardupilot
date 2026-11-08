#!/usr/bin/env bash
# Phase 19 companion IOMCU compile-only validation. NO FLASH, NO AUTO INSTALL.
set -euo pipefail
cd "$(dirname "$0")/../../.."

# The dedicated F103 image uses the compile-time MAIN3/PB8 inhibit. Do not
# replace generic IO assets in Tools/IO_Firmware or use stock IO as proof.
python3 libraries/AP_SpektrumSmartESC/tests/run_host_tests.py

./waf configure --board iomcu-f103-spktsmart
./waf iofirmware

dir="build/iomcu-f103-spktsmart/bin"
for image in iofirmware_lowpolh.bin iofirmware_highpolh.bin; do
    test -s "$dir/$image"
    sha256sum "$dir/$image"
done

printf '\nPHASE19 IOMCU BUILD PASS: separate companion binaries only. NOT FLASHED; output isolation NOT yet verified on hardware.\n'
