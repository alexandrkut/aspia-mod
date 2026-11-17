#!/usr/bin/env bash
#
# Puts the Qt sources into the downloads folder of vcpkg before the build starts.
#
# download.qt.io does not answer from this machine, and vcpkg would sit through the
# timeout of every single file before it tries the mirror that does work. The files are
# the same on the mirror, and vcpkg checks the hash of whatever it finds in the folder,
# so putting them there by hand is the same thing as letting vcpkg fetch them.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

readonly DOWNLOADS="${REPO_ROOT}/vcpkg4aspia/downloads"
readonly MIRROR="https://mirrors.ocf.berkeley.edu/qt/archive/qt/6.11/6.11.1/submodules"
readonly VERSION="6.11.1"

mkdir -p "${DOWNLOADS}"
cd "${DOWNLOADS}"

for module in qtbase qttools qtsvg; do
    name="${module}-everywhere-src-${VERSION}.tar.xz"

    if [[ -f "${name}" ]]; then
        echo "already there: ${name}"
        continue
    fi

    echo "downloading ${name}"
    curl -fL --retry 3 --retry-delay 5 -o "${name}.part" "${MIRROR}/${name}"

    mv "${name}.part" "${name}"
done

echo "=== hashes ==="
for module in qtbase qttools qtsvg; do
    name="${module}-everywhere-src-${VERSION}.tar.xz"
    hash=$(sha512sum "${name}" | cut -d ' ' -f 1)
    echo "${name}: ${hash}"
done

ls -la "${DOWNLOADS}" | grep -E "qt(base|tools|svg)"
