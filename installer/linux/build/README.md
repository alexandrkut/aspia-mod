Building the router for Linux
============================

The scripts here build the router on Debian 10 (buster) and on anything close to it. The
dependencies are not taken from the distribution: they come from vcpkg4aspia, the fork of
vcpkg that Aspia builds against, so that the result matches what the project ships rather
than whatever the distribution happens to have this month. That is also why this takes
hours: Qt 6 is built from source, together with every other dependency, once.

The order
---------

    ./setup-toolchain.sh      # the compiler, CMake and ninja the system is too old for
    ./prefetch-qt.sh          # the sources of Qt, from a mirror that answers
    ./build-linux-router.sh   # the dependencies and the router

`setup-toolchain.sh` needs root, because it installs a compiler, CMake and ninja, and
because it puts a swap file on the machine. The other two do not.

What the toolchain is for
-------------------------

Debian 10 is from 2019, and the project needs things that did not exist then:

| | Debian 10 | What is needed | Why |
|---|---|---|---|
| GCC | 8.3 | 12.2 | Qt 6.11 wants C++20 from a much newer compiler |
| CMake | 3.13 | 3.31 | the presets of the project need 3.30 |
| ninja | 1.8 | 1.12 | CMake wants a newer one to talk to |
| Autoconf | 2.69 | 2.71 | gperf asks for 2.70 or newer |
| Python | 3.7 | 3.11 | systemd 260 asks for 3.9 or newer |

They are built from source, against the headers of the system, so that what comes out
still speaks the glibc of that system: a binary built this way on buster runs on buster,
on Ubuntu 18.04 and on anything newer.

The fork of vcpkg
----------------

The fork of vcpkg that Aspia builds against is in `vcpkg4aspia/` in this repository, so a
change to a port of it is an ordinary commit and the build needs nothing put on it by
hand.

The router without a window
----------------------------

    ./build-linux-router.sh --router-only

The router is a daemon: it opens sockets, keeps a database and speaks the protocol.
Nothing in it draws anything, so the build leaves out QtGui, the widgets, the SVG, the
codecs of the picture and the sound, and the X11 and Wayland libraries. What is left is
built from the manifest in `router-manifest/` and the preset `local-linux-x64-router`.

The check
---------

    ./verify.sh

It tells what the binary needs from the system, which glibc it asks for, and builds the
package of it.
