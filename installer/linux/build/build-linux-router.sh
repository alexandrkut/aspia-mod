#!/usr/bin/env bash
#
# Builds the Aspia router for Linux, on Debian or anything close to it.
#
# The binary only - no package. What comes out is builds/local-linux-x64-router/Release/aspia_router,
# an executable that needs nothing but the C library of the system.
#
# The dependencies are not taken from the distribution. They come from vcpkg4aspia, the fork of
# vcpkg that Aspia builds against, so the result matches what the project ships rather than
# whatever Debian happens to have this month. That is also why this takes hours: Qt 6 is built
# from source here, together with every other dependency, once, into the build tree.
#
# Usage:
#   ./build-linux-router.sh                 build the router
#   ./build-linux-router.sh --router-only   build the router alone, without the parts that draw
#   ./build-linux-router.sh --deps-only     build the dependencies and stop
#   ./build-linux-router.sh --jobs 4        limit the parallelism
#
# Re-running is cheap: vcppkg keeps what it has built, and ninja keeps what it has compiled.

set -euo pipefail

# The sources are the repository this script lives in. The toolchain, the cache of the built
# packages and the binaries are kept outside of it, in the directory above.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR

readonly REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

# Where the toolchain and the cache of the built packages live. It is the directory above the
# sources by default, so that neither of them is ever part of the sources.
WORK_DIR="${WORK_DIR:-$(dirname "${REPO_ROOT}")}"
readonly WORK_DIR

readonly TRIPLET="x64-linux"
PRESET="local-linux-x64"

# ---------------------------------------------------------------------------------------------
# What was asked for
# ---------------------------------------------------------------------------------------------

deps_only=0
jobs=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --deps-only) deps_only=1; shift ;;
        --router-only) PRESET="local-linux-x64-router"; shift ;;
        --jobs) jobs="$2"; shift 2 ;;
        --jobs=*) jobs="${1#--jobs=}"; shift ;;
        -h|--help) sed -n '3,22p' "$0"; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done

# ---------------------------------------------------------------------------------------------
# Where the sources are
# ---------------------------------------------------------------------------------------------

readonly SRC_DIR="${REPO_ROOT}"
readonly VCPKG_DIR="${SRC_DIR}/vcpkg4aspia"
readonly TOOLCHAIN_ENV="${WORK_DIR}/toolchain/env.sh"

# The toolchain built by setup-toolchain.sh: a compiler new enough for C++20, a CMake
# new enough for the presets, and a ninja new enough for CMake to talk to. Without it
# buster is too old to build the project.
if [[ -f "${TOOLCHAIN_ENV}" ]]; then
    # shellcheck disable=SC1090
    source "${TOOLCHAIN_ENV}"
fi

if [[ ! -f "${SRC_DIR}/CMakePresets.json" ]]; then
    echo "The sources are not at ${SRC_DIR}." >&2
    echo "Set WORK_DIR to the directory that holds the aspia checkout, or clone it there:" >&2
    echo "    sudo mkdir -p ${WORK_DIR} && sudo chown \$USER ${WORK_DIR}" >&2
    echo "    git clone https://github.com/alexandrkut/aspia.git ${SRC_DIR}" >&2
    exit 1
fi

# ---------------------------------------------------------------------------------------------
# Build tools that are not in the base image
# ---------------------------------------------------------------------------------------------

# The X11 and Wayland development files are needed by Qt, not by the router itself: the router
# draws nothing, but it is compiled in the same tree as the parts that do.
readonly APT_PACKAGES_COMMON=(
    build-essential
    cmake
    ninja-build
    git
    curl
    zip
    unzip
    tar
    autoconf
    autoconf-archive
    automake
    libltdl-dev
    libtool
    pkg-config
    bison
    flex
    gperf
    python3
    python3-jinja2
    python3-venv
    libdbus-1-dev
    libicu-dev
    libsystemd-dev
    libssl-dev
    zlib1g-dev
)

readonly APT_PACKAGES_GUI=(
    libx11-dev
    libxdamage-dev
    libxext-dev
    libxfixes-dev
    libxi-dev
    libxrandr-dev
    libxtst-dev
    libxrender-dev
    libxkbcommon-dev
    libxkbcommon-x11-dev
    libxcb1-dev
    libxcb-image0-dev
    libxcb-keysyms1-dev
    libxcb-render-util0-dev
    libxcb-util-dev
    libxcb-util-wm-dev
    libxcb-xkb-dev
    libsm-dev
    libice-dev
    libgl1-mesa-dev
    libglu1-mesa-dev
    libfontconfig1-dev
    libfreetype-dev
    wayland-protocols
    libwayland-dev
)

install_packages()
{
    local missing=()
    local packages=("${APT_PACKAGES_COMMON[@]}")

    # A router alone draws nothing, so the files of the window system are not needed.
    if [[ "${PRESET}" != "local-linux-x64-router" ]]; then
        packages+=("${APT_PACKAGES_GUI[@]}")
    fi

    # Only the packages that are actually absent are installed, so a re-run does not repeat the
    # whole list and does not need the packages to be in the index any more.
    for package in "${packages[@]}"; do
        if ! dpkg-query -W -f='${Status}' "${package}" 2>/dev/null | grep -q "ok installed"; then
            missing+=("${package}")
        fi
    done

    if [[ ${#missing[@]} -eq 0 ]]; then
        echo "Build dependencies are in place."
        return
    fi

    echo "Installing ${#missing[@]} build dependencies..."

    # Root is needed for the packages, but not for the build itself, so the packages are
    # installed with sudo and the build carries on as the ordinary user. That keeps the
    # build tree owned by the user who started it.
    local apt="apt-get"

    if [[ "$(id -u)" -ne 0 ]]; then
        if ! command -v sudo > /dev/null; then
            echo "Root is needed to install packages. Re-run with sudo, or:" >&2
            echo "    sudo apt-get install -y ${missing[*]}" >&2
            exit 1
        fi

        # Sudo that asks for a password would leave the build waiting for a keypress that
        # never comes, so the check is made before the build starts.
        if ! sudo -n true 2> /dev/null; then
            echo "Root is needed to install packages, and sudo wants a password." >&2
            echo "Run 'sudo -v' first, or install the packages by hand:" >&2
            echo "    sudo apt-get install -y ${missing[*]}" >&2
            exit 1
        fi

        apt="sudo -n apt-get"
    fi

    export DEBIAN_FRONTEND=noninteractive
    ${apt} update
    # libgl1-mesa-dev is a transitional package on some releases and is absent on others;
    # the real one is libgl-dev there, and neither is required to build the router.
    ${apt} install -y --no-install-recommends "${missing[@]}" || \
        ${apt} install -y --no-install-recommends "${missing[@]/libgl1-mesa-dev/libgl-dev}" || \
        ${apt} install -y --no-install-recommends "${missing[@]}"
}

# ---------------------------------------------------------------------------------------------
# vcpkg
# ---------------------------------------------------------------------------------------------

bootstrap_vcpkg()
{
    if [[ ! -x "${VCPKG_DIR}/vcpkg" ]]; then
        echo "Bootstrapping vcpkg. This happens once and downloads a toolchain."
        (cd "${VCPKG_DIR}" && ./bootstrap-vcpkg.sh -disableMetrics)
    fi

    export VCPKG_ROOT="${VCPKG_DIR}"
    export VCPKG_TARGET_TRIPLET="${TRIPLET}"
    export VCPKG_HOST_TRIPLET="${TRIPLET}"
    export VCPKG_TRIPLET="${TRIPLET}"
    export VCPKG_INSTALLED_DIR="${SRC_DIR}/builds/${PRESET}/vcpkg_installed"
    export VCPKG_BINARY_SOURCES="default,readwrite"

    # How many ports are built at the same time. vcpkg would take the number of cores,
    # but a compiler that works on Qt needs a gigabyte or more of memory for itself, and
    # a machine that has less than that swaps. Fewer ports at a time is slower, but it
    # gets to the end.
    if [[ -z "${VCPKG_MAX_CONCURRENCY:-}" ]]; then
        local cores
        cores="$(nproc)"

        if [[ "${cores}" -gt 4 ]]; then
            cores=4
        fi

        export VCPKG_MAX_CONCURRENCY="${cores}"
    fi

    echo "Building ${VCPKG_MAX_CONCURRENCY} port(s) at a time."

    # A build cache shared between runs turns the second build of a dependency into a copy
    # instead of a rebuild. Point it somewhere with room if the default is tight.
    export VCPKG_DEFAULT_BINARY_CACHE="${VCPKG_DEFAULT_BINARY_CACHE:-${WORK_DIR}/vcpkg-bincache}"
    mkdir -p "${VCPKG_DEFAULT_BINARY_CACHE}"

    "${VCPKG_DIR}/vcpkg" version
}

# ---------------------------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------------------------

configure()
{
    # The preset carries the triplet, the toolchain file and the install directory, so nothing of
    # that is spelled out here: what the project expects for Linux is what it gets.
    echo "Configuring. vcpkg builds every dependency here, which is the long part."
    (cd "${SRC_DIR}" && cmake --preset "${PRESET}")
}

build_router()
{
    local args=(--preset "${PRESET}" --config Release --target aspia_router)

    if [[ -n "${jobs}" ]]; then
        args+=(--parallel "${jobs}")
    fi

    (cd "${SRC_DIR}" && cmake --build "${args[@]}")
}

report()
{
    local binary="${SRC_DIR}/builds/${PRESET}/Release/aspia_router"

    if [[ ! -x "${binary}" ]]; then
        echo "The router was not produced." >&2
        return 1
    fi

    echo
    echo "##################################################"
    echo "Aspia router for Linux is ready:"
    echo "  ${binary}"
    echo "  $(du -h "${binary}" | cut -f1)"
    echo "  $(file -b "${binary}" | cut -c1-80)"
    echo
    echo "The binary needs the C library of the system and nothing else."
    echo "Next to it it needs its configuration, which it creates on first start:"
    echo "  \$XDG_CONFIG_HOME/aspia/router.conf"
}

main()
{
    install_packages
    bootstrap_vcpkg

    configure

    if [[ ${deps_only} -eq 1 ]]; then
        echo "Dependencies are built. Run again without --deps-only to build the router."
        return
    fi

    build_router
    report
}

main "$@"
