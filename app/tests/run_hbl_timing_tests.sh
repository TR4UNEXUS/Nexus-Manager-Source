#!/bin/sh
# Run production transport/timing/trace code on Linux/WSL without a Switch.
set -eu
cd "$(dirname "$0")/.."
out=build/homebrew-tests
mkdir -p "$out"
gcc -std=c11 -O1 -Wall -Wextra -Werror -Itests/hbl_mock -Isphaira/include -Ihbl/source tests/hbl_timing_tests.c hbl/source/nexus_timing.c -o "$out/hbl_timing_tests"
"$out/hbl_timing_tests"
gcc -std=c11 -O1 -Wall -Wextra -Werror -DNEXUS_PATH_CHECKS -Itests/hbl_mock -Ihbl/source tests/hbl_trace_tests.c hbl/source/nexus_trace.c -o "$out/hbl_trace_tests"
for mode in normal open-fail create-fail write-fail timed unresolved; do
  "$out/hbl_trace_tests" "$mode"
done
gcc -std=gnu11 -O1 -Wall -Wextra -Werror -Itests/hbl_mock -Isphaira/include -Ihbl/source tests/hbl_usb_read_tests.c hbl/source/nexus_vsd.c -o "$out/hbl_usb_read_tests"
"$out/hbl_usb_read_tests"
gcc -std=c11 -O1 -Itests/hbl_mock -Isphaira/include -Ihbl/source tests/hbl_transport_tests.c hbl/source/nexus_vsd.c -o "$out/hbl_transport_tests"
"$out/hbl_transport_tests"
python3 tests/bootstrap_registration_tests.py
python3 tests/hbl_startup_tests.py
python3 tests/forwarder_romfs_tests.py --compiler g++ --libnx ../dependencies/sdk/libnx
NEXUS_TEST_BUILD_DIR="$PWD/build/NexusPorts" python3 tests/forwarder_npdm_tests.py --compiler g++
