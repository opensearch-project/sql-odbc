#!/bin/bash
# Build the driver and unit tests on Linux (x86_64) against unixODBC.
# Requires: cmake, a C/C++ toolchain, vcpkg on PATH (or VCPKG_ROOT), and
# autoconf/automake/libtool (with libltdl sources) for vcpkg's unixodbc port.
set -euo pipefail

VCPKG="${VCPKG_ROOT:+$VCPKG_ROOT/}vcpkg"

cd src
"$VCPKG" install
cd ..

PREFIX_PATH="$(pwd)/src/vcpkg_installed/x64-linux"
cmake -S src -B cmake-build64 \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="${PREFIX_PATH}" \
  -DWARNINGS_AS_ERRORS=OFF \
  -DBUILD_WITH_TESTS=ON

# Integration and performance tests don't compile on unixODBC yet (SQLWCHAR is
# 2 bytes, wchar_t is 4), so only build the driver and unit tests for now.
cmake --build cmake-build64 --parallel "$(nproc)" --target \
  sqlodbc ut_critical_section ut_conn ut_aws_sdk_cpp ut_mylog ut_parse_result ut_convert
