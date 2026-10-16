#!/usr/bin/env bash
set -x
cd /d/zephyr4_0/810_driver_test/projects/i2cf_test/tests/case_wave
./test.sh
echo "=== CASE_WAVE RC=$? ==="
cd /d/zephyr4_0/810_driver_test/projects/i2cf_test/tests/case_default
./test.sh
echo "=== CASE_DEFAULT RC=$? ==="
echo "=== BUILD BOTH DONE ==="
