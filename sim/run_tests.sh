#!/bin/sh
# Build and run the EMG claw control-logic simulation tests.
# Tests the exact emg_control.h that ships in ../emg_claw/.
set -e
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -O1 test_emg_control.cpp -o /tmp/test_emg_control
/tmp/test_emg_control
