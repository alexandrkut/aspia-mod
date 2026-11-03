#!/usr/bin/env bash
#
# Checks the router that was built and puts it in a package.
#
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

readonly BUILD_DIR="${REPO_ROOT}/builds/local-linux-x64-router/Release"
readonly BINARY="${BUILD_DIR}/aspia_router"

if [[ ! -x "${BINARY}" ]]; then
    echo "The router was not built: ${BINARY} is missing."
    exit 1
fi

echo "=== the binary ==="
ls -la "${BINARY}"
file "${BINARY}"
du -h "${BINARY}" | cut -f1

echo
echo "=== what it needs from the system ==="
readelf -d "${BINARY}" | grep NEEDED

echo
echo "=== the oldest glibc it asks for ==="
objdump -T "${BINARY}" | sed -n 's/.*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -V -u | tail -3

echo
echo "=== the newest C++ runtime it asks for ==="
objdump -T "${BINARY}" | sed -n 's/.*GLIBCXX_\([0-9][0-9.]*\).*/\1/p' | sort -V -u | tail -3

echo
echo "=== the version it reports ==="
"${BINARY}" --version 2>&1 | head -3

echo
echo "=== the package ==="
cd "${REPO_ROOT}"
./installer/linux/build_packages.sh --build-dir "${BUILD_DIR}" --component router --format deb
