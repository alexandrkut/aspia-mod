#!/usr/bin/env bash
#
# A newer GNU Autoconf.
#
# Debian 10 ships Autoconf 2.69, and some of the packages that vcpkg builds ask for 2.70 or
# newer. Autoconf is a program that writes programs, so a newer one is installed into
# /usr/local, where it comes before the one of the system.
#
set -euo pipefail

# The toolchain is kept outside of the sources, in the directory above them.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

WORK_DIR="${WORK_DIR:-$(dirname "${REPO_ROOT}")}"
readonly WORK_DIR

readonly VERSION="2.71"
readonly SRC_DIR="${WORK_DIR}/toolchain/src"
readonly PREFIX="/usr/local"

if [[ -x "${PREFIX}/bin/autoreconf" ]] && "${PREFIX}/bin/autoreconf" --version | head -1 | grep -q "2.71"; then
    echo "Autoconf ${VERSION} is already installed."
    exit 0
fi

mkdir -p "${SRC_DIR}"
cd "${SRC_DIR}"

if [[ ! -f "autoconf-${VERSION}.tar.xz" ]]; then
    curl -fL --retry 3 -o "autoconf-${VERSION}.tar.xz" \
        "https://ftpmirror.gnu.org/gnu/autoconf/autoconf-${VERSION}.tar.xz"
fi

xz -t "autoconf-${VERSION}.tar.xz"

rm -rf "autoconf-${VERSION}"
tar xf "autoconf-${VERSION}.tar.xz"
cd "autoconf-${VERSION}"

./configure --prefix="${PREFIX}"
make -j"$(nproc)"
make install

hash -r
autoreconf --version | head -1
