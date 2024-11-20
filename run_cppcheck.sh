#!/bin/bash

RESULTS_FILE=cppcheck-results.log

# --force: force checks all define combinations (default max is 12)
# -iaws-sdk-cpp: avoid checking AWS C++ SDK source files in our repo
# -UWIN32: do not check WIN32-defined codepaths; this would throw errors on Mac
# --check-level: use exhaustive checking for pipeline jobs; can disable during active development for quicker results
# --inline-suppr: enable inline error suppression
# -i: ignore CMake and installed files

cppcheck \
  --force -iaws-sdk-cpp -UWIN32 \
  --check-level=exhaustive \
  --inline-suppr \
  -i "src/vcpkg_installed" \
  -i "src/cmake-build-debug" \
  ./src 2> ${RESULTS_FILE}

if [ -s ${RESULTS_FILE} ]; then
    echo "!! Cppcheck errors found! Check ${RESULTS_FILE} for details."
    exit 1
else
    echo "No Cppcheck errors found."
fi
