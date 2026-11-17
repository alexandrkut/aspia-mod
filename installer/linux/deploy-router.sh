#!/bin/sh
#
# Puts the router on the machine without a person at the keyboard.
#
# The package of the router is installed, the configuration is made, the service is
# registered and started. The name of the user and the password of the administrator are
# written to the standard output, because they are shown only once and the person who
# installs the router needs them.
#
# Usage:
#   sudo ./deploy-router.sh <package.deb>
#
# The package of the router is built by installer/linux/build_packages.sh, and it is
# installed with dpkg, which says nothing while it works.

set -e

if [ "$(id -u)" -ne 0 ]; then
    echo "Root is needed. Run it with sudo." >&2
    exit 1
fi

package="${1:-}"

if [ -z "${package}" ]; then
    echo "The package of the router is needed: $0 <package.deb>" >&2
    exit 1
fi

if [ ! -f "${package}" ]; then
    echo "No such file: ${package}" >&2
    exit 1
fi

echo "Installing ${package}..."
dpkg -i "${package}"

echo "Making the configuration..."
if ! /usr/bin/aspia_router --create-config; then
    echo "The configuration exists already, it is left as it is."
fi

echo "Registering the service..."
/usr/bin/aspia_router --install

if [ -d /run/systemd/system ]; then
    echo "Starting the service..."
    systemctl enable aspia-router.service > /dev/null 2>&1 || true
    systemctl start aspia-router.service
fi

echo
echo "The router is running."
