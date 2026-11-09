#!/usr/bin/env bash
#
# A newer Python.
#
# Debian 10 ships Python 3.7, and systemd 260, which vcpkg builds for the database bus,
# asks for Python 3.9 or newer. The new Python is installed into /usr/local, where it comes
# before the one of the system, and the programs of the system keep their own interpreter
# because they name it by its full path.
#
set -euo pipefail

# The toolchain is kept outside of the sources, in the directory above them.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

WORK_DIR="${WORK_DIR:-$(dirname "${REPO_ROOT}")}"
readonly WORK_DIR

readonly VERSION="3.11.9"
readonly SRC_DIR="${WORK_DIR}/toolchain/src"
readonly PREFIX="/usr/local"

if [[ -x "${PREFIX}/bin/python3" ]] && "${PREFIX}/bin/python3" --version | grep -q "3.11"; then
    echo "Python ${VERSION} is already installed."
    exit 0
fi

mkdir -p "${SRC_DIR}"
cd "${SRC_DIR}"

if [[ ! -f "Python-${VERSION}.tar.xz" ]]; then
    curl -fL --retry 3 -o "Python-${VERSION}.tar.xz" \
        "https://www.python.org/ftp/python/${VERSION}/Python-${VERSION}.tar.xz"
fi

xz -t "Python-${VERSION}.tar.xz"

rm -rf "Python-${VERSION}"
tar xf "Python-${VERSION}.tar.xz"
cd "Python-${VERSION}"

./configure --prefix="${PREFIX}" --with-ensurepip=install
make -j"$(nproc)"
make install

hash -r
"${PREFIX}/bin/python3" --version
