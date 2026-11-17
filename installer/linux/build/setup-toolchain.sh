#!/usr/bin/env bash
#
# Prepares a build environment for the Aspia router on Debian 10 (buster).
#
# Three things are missing on buster and cannot be taken from the archive:
#
#   * GCC 12    - buster has 8.3, and the project needs C++20, which Qt 6.11 wants
#                from a much newer compiler. Built from source here, against the
#                headers of buster, so the result still speaks glibc 2.28.
#   * CMake 3.31 - buster has 3.13, the project needs 3.30 for its presets.
#   * ninja 1.12  - buster has 1.8, which is older than what CMake wants to see.
#
# Everything lands in the toolchain directory next to the sources, and is safe to
# delete and rebuild.
#
# Usage:
#   ./setup-toolchain.sh              build whatever is missing
#   ./setup-toolchain.sh --jobs 4     limit the parallelism
#
# The build is slow on a box with little memory: every step honours --jobs, and
# the log is written to the build log of the toolchain directory.

set -euo pipefail

# The toolchain is kept outside of the sources, in the directory above them.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

WORK_DIR="${WORK_DIR:-$(dirname "${REPO_ROOT}")}"
readonly WORK_DIR

readonly TOOLCHAIN_DIR="${WORK_DIR}/toolchain"
readonly SRC_DIR="${TOOLCHAIN_DIR}/src"
readonly GCC_VERSION="12.2.0"
readonly CMAKE_VERSION="3.31.6"
readonly NINJA_VERSION="1.12.1"
readonly SWAP_FILE="/swapfile-toolchain"
readonly SWAP_SIZE="16G"

jobs="$(nproc)"
while [[ $# -gt 0 ]]; do
    case "$1" in
        --jobs) jobs="$2"; shift 2 ;;
        --jobs=*) jobs="${1#--jobs=}"; shift ;;
        -h|--help) sed -n '3,22p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

log()
{
    echo "[$(date '+%H:%M:%S')] $*"
}

need_root()
{
    if [[ "$(id -u)" -ne 0 ]]; then
        echo "Root is needed. Re-run with sudo." >&2
        exit 1
    fi
}

# ---------------------------------------------------------------------------------------------
# Swap.
#
# The box this runs on may have its memory taken away by the hypervisor, and compiling
# Qt asks for more than what is left. Swap is what keeps the build from being killed.
# ---------------------------------------------------------------------------------------------

setup_swap()
{
    if swapon --show=NAME --noheadings | grep -q "${SWAP_FILE}"; then
        log "Swap is in place."
        return
    fi

    log "Creating ${SWAP_SIZE} of swap."

    if [[ ! -f "${SWAP_FILE}" ]]; then
        fallocate -l "${SWAP_SIZE}" "${SWAP_FILE}" || dd if=/dev/zero of="${SWAP_FILE}" bs=1M count=16384
    fi

    chmod 600 "${SWAP_FILE}"
    mkswap "${SWAP_FILE}"
    swapon "${SWAP_FILE}"

    if ! grep -q "${SWAP_FILE}" /etc/fstab; then
        echo "${SWAP_FILE} none swap sw 0 0" >> /etc/fstab
    fi

    free -h
}

# ---------------------------------------------------------------------------------------------
# Packages.
# ---------------------------------------------------------------------------------------------

setup_packages()
{
    export DEBIAN_FRONTEND=noninteractive

    # The archive drops Valid-Until, but an expired one in the index is refused unless
    # the check is turned off.
    apt-get -o Acquire::Check-Valid-Until=false update || true

    apt-get install -y --no-install-recommends \
        autoconf automake bison build-essential ca-certificates curl flex gperf git \
        libtool make patch pkg-config python3 python3-jinja2 texinfo wget xz-utils
}

# ---------------------------------------------------------------------------------------------
# GCC.
# ---------------------------------------------------------------------------------------------

setup_gcc()
{
    local prefix="${TOOLCHAIN_DIR}/gcc-${GCC_VERSION}"

    if [[ -x "${prefix}/bin/g++" ]]; then
        log "GCC ${GCC_VERSION} is already built."
        return
    fi

    log "Building GCC ${GCC_VERSION}. This is the long one."

    mkdir -p "${SRC_DIR}"
    cd "${SRC_DIR}"

    # The size is what tells a finished download from a half-written one: the file is
    # only written to disk as it comes, so a run that was cut short leaves a short file.
    if [[ ! -f "gcc-${GCC_VERSION}.tar.xz" ]] || \
       [[ "$(stat -c %s "gcc-${GCC_VERSION}.tar.xz")" -ne 84645292 ]]; then
        rm -f "gcc-${GCC_VERSION}.tar.xz"
        curl -fL --retry 3 -o "gcc-${GCC_VERSION}.tar.xz" \
            "https://ftpmirror.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/gcc-${GCC_VERSION}.tar.xz"
    fi

    xz -t "gcc-${GCC_VERSION}.tar.xz"

    # A tree that is already there is a tree that was already configured, and a configured
    # tree is one that can be built on: make picks up where it stopped.
    if [[ ! -d "gcc-${GCC_VERSION}" ]]; then
        tar xf "gcc-${GCC_VERSION}.tar.xz"
    fi

    cd "gcc-${GCC_VERSION}"

    # The Gnu MP, MPFR and GMP sources that GCC links against.
    if [[ ! -d gmp ]] || [[ ! -d mpfr ]] || [[ ! -d mpc ]] || [[ ! -d isl ]]; then
        ./contrib/download_prerequisites
    fi

    if [[ ! -f build/config.status ]]; then
        mkdir -p build
        cd build

        # --disable-bootstrap uses the compiler of the machine as the starting point, which
        # saves one full round of the build. The compiler of the machine is GCC 8.3, and that
        # is enough to build GCC 12.
        ../configure \
            --prefix="${prefix}" \
            --enable-languages=c,c++ \
            --disable-multilib \
            --disable-bootstrap \
            --enable-checking=release \
            --with-system-zlib
    else
        cd build
    fi

    make -j"${jobs}"
    make install

    "${prefix}/bin/g++" --version
}

# ---------------------------------------------------------------------------------------------
# CMake.
# ---------------------------------------------------------------------------------------------

setup_cmake()
{
    local prefix="${TOOLCHAIN_DIR}/cmake-${CMAKE_VERSION}"

    if [[ -x "${prefix}/bin/cmake" ]]; then
        log "CMake ${CMAKE_VERSION} is already built."
        return
    fi

    log "Building CMake ${CMAKE_VERSION}."

    mkdir -p "${SRC_DIR}"
    cd "${SRC_DIR}"

    if [[ ! -f "cmake-${CMAKE_VERSION}.tar.gz" ]]; then
        curl -fL -o "cmake-${CMAKE_VERSION}.tar.gz" \
            "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}.tar.gz"
    fi

    rm -rf "cmake-${CMAKE_VERSION}"
    tar xf "cmake-${CMAKE_VERSION}.tar.gz"
    cd "cmake-${CMAKE_VERSION}"

    ./bootstrap --prefix="${prefix}" --parallel="${jobs}"
    make -j"${jobs}"
    make install

    "${prefix}/bin/cmake" --version
}

# ---------------------------------------------------------------------------------------------
# ninja.
# ---------------------------------------------------------------------------------------------

setup_ninja()
{
    local prefix="${TOOLCHAIN_DIR}/ninja-${NINJA_VERSION}"

    if [[ -x "${prefix}/bin/ninja" ]]; then
        log "ninja ${NINJA_VERSION} is already built."
        return
    fi

    log "Building ninja ${NINJA_VERSION}."

    mkdir -p "${SRC_DIR}"
    cd "${SRC_DIR}"

    if [[ ! -f "ninja-${NINJA_VERSION}.tar.gz" ]]; then
        curl -fL -o "ninja-${NINJA_VERSION}.tar.gz" \
            "https://github.com/ninja-build/ninja/archive/refs/tags/v${NINJA_VERSION}.tar.gz"
    fi

    rm -rf "ninja-${NINJA_VERSION}"
    tar xf "ninja-${NINJA_VERSION}.tar.gz"
    cd "ninja-${NINJA_VERSION}"

    "${TOOLCHAIN_DIR}/cmake-${CMAKE_VERSION}/bin/cmake" \
        -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${prefix}"

    "${TOOLCHAIN_DIR}/cmake-${CMAKE_VERSION}/bin/cmake" --build build -j"${jobs}"
    "${TOOLCHAIN_DIR}/cmake-${CMAKE_VERSION}/bin/cmake" --install build

    "${prefix}/bin/ninja" --version
}

# ---------------------------------------------------------------------------------------------
# Autoconf.
#
# Debian 10 ships Autoconf 2.69, and some of the packages that vcpkg builds ask for 2.70 or
# newer. Autoconf is a program that writes programs, so a newer one is installed into
# /usr/local, where it comes before the one of the system.
# ---------------------------------------------------------------------------------------------

setup_autoconf()
{
    local version="2.71"

    if [[ -x "/usr/local/bin/autoreconf" ]] && \
       /usr/local/bin/autoreconf --version | head -1 | grep -q "${version}"; then
        log "Autoconf ${version} is already installed."
        return
    fi

    log "Building Autoconf ${version}."

    mkdir -p "${SRC_DIR}"
    cd "${SRC_DIR}"

    if [[ ! -f "autoconf-${version}.tar.xz" ]]; then
        curl -fL --retry 3 -o "autoconf-${version}.tar.xz" \
            "https://ftpmirror.gnu.org/gnu/autoconf/autoconf-${version}.tar.xz"
    fi

    xz -t "autoconf-${version}.tar.xz"

    rm -rf "autoconf-${version}"
    tar xf "autoconf-${version}.tar.xz"
    cd "autoconf-${version}"

    ./configure --prefix="/usr/local"
    make -j"${jobs}"
    make install

    hash -r
    /usr/local/bin/autoreconf --version | head -1
}

# ---------------------------------------------------------------------------------------------
# The environment file the build script reads.
# ---------------------------------------------------------------------------------------------

write_env()
{
    cat > "${TOOLCHAIN_DIR}/env.sh" <<ENV
# The toolchain built by setup-toolchain.sh. Source this before building.
export PATH="${TOOLCHAIN_DIR}/gcc-${GCC_VERSION}/bin:${TOOLCHAIN_DIR}/cmake-${CMAKE_VERSION}/bin:${TOOLCHAIN_DIR}/ninja-${NINJA_VERSION}/bin:\$PATH"
export CC="${TOOLCHAIN_DIR}/gcc-${GCC_VERSION}/bin/gcc"
export CXX="${TOOLCHAIN_DIR}/gcc-${GCC_VERSION}/bin/g++"

# The programs that the build runs were made by the new compiler, and the new compiler
# needs a newer C++ runtime than the system has. The loader is told where to look.
export LD_LIBRARY_PATH="${TOOLCHAIN_DIR}/gcc-${GCC_VERSION}/lib64:\${LD_LIBRARY_PATH:-}"
ENV

    # The same, for the programs that the build runs without the environment of the shell:
    # the loader itself is told where the runtime of the compiler is.
    echo "${TOOLCHAIN_DIR}/gcc-${GCC_VERSION}/lib64" > /etc/ld.so.conf.d/aspia-toolchain.conf
    ldconfig || /sbin/ldconfig

    log "Wrote ${TOOLCHAIN_DIR}/env.sh"
}

main()
{
    need_root

    mkdir -p "${SRC_DIR}"

    setup_swap
    setup_packages
    setup_gcc
    setup_cmake
    setup_ninja
    setup_autoconf
    write_env

    # The build itself runs as the ordinary user, so the toolchain has to be readable
    # and writable by that user.
    if [[ -n "${SUDO_USER:-}" ]]; then
        chown -R "${SUDO_USER}:${SUDO_USER}" "${TOOLCHAIN_DIR}"
    fi

    log "The toolchain is ready. Source ${TOOLCHAIN_DIR}/env.sh before building."
}

main "$@"
