#!/usr/bin/env bash
# SpektrumRC/SRXL2 actual upstream parser, pinned to a reviewed MIT source SHA.
# Host-only: no serial hardware, motor outputs, or ArduPilot flashing.
set -euo pipefail
cd "$(dirname "$0")/../../.."

readonly upstream_sha="6251dd14fbc95f262ecbc22e38ad39a8b907e411"
readonly out="build/spektrum-smart-host/vendor-interop"
readonly vendor="$out/SRXL2"
readonly tests="libraries/AP_SpektrumSmartESC/tests"
readonly lib="libraries/AP_SpektrumSmartESC"
mkdir -p "$out"
if [[ ! -d "$vendor/.git" ]]; then
    mkdir -p "$vendor"
    git -C "$vendor" init -q
fi
if [[ "$(git -C "$vendor" rev-parse HEAD 2>/dev/null || true)" != "$upstream_sha" ]]; then
    git -C "$vendor" fetch --quiet --depth=1 https://github.com/SpektrumRC/SRXL2.git "$upstream_sha"
    git -C "$vendor" checkout --quiet --detach FETCH_HEAD
fi
if [[ "$(git -C "$vendor" rev-parse HEAD)" != "$upstream_sha" ]]; then
    echo "Spektrum upstream source changed: FAIL" >&2
    exit 1
fi

${CC:-cc} -std=c11 -w -I"$vendor/Source" -I"$tests/vendor_srxl2_shim" \
    -c "$vendor/Source/spm_srxl.c" -o "$out/spm_srxl.o"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -I"$vendor/Source" -I"$tests/vendor_srxl2_shim" \
    -c "$tests/vendor_srxl2_adapter.c" -o "$out/adapter.o"
${CXX:-c++} -std=c++11 -Wall -Wextra -Werror -DSPKTSMART_STANDALONE_TEST \
    "$lib/AP_SpektrumSmartESC.cpp" \
    "$lib/AP_SpektrumSmartESC_ChannelData.cpp" \
    "$tests/test_spektrum_vendor_srxl2.cpp" \
    "$out/spm_srxl.o" "$out/adapter.o" -o "$out/test_vendor_interop"
"$out/test_vendor_interop"
echo "Vendor SHA verified: $upstream_sha"
