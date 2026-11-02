Building on Windows
===================

The scripts here build Aspia on Windows the way the CI does, and they fetch what the
build needs before it starts. The sources are the repository they are in; the cache of
the fetched sources, the WiX tools and the log of the build are the directory above it.

The order
---------

    .\fetch-msys2.ps1        # the toolchain of the ports that are built with jom
    .\fetch-sources.ps1      # the sources of the remaining vcpkg ports, with the hashes checked
    .\build-win.ps1 all      # configure, build and test
    .\build-installer.ps1    # the MSI of the client or of the router

`build-win.ps1` takes the stage as an argument: `configure`, `build`, `test`, `clean` or
`all`. It needs Visual Studio 2022 Build Tools (MSVC x14.44, Windows SDK) and CMake and
Ninja.

Why the sources are fetched by hand
-----------------------------------

vcpkg downloads the sources of its ports one at a time, and the mirrors it uses stall
now and then: a connection is accepted and the body never comes, and vcpkg waits instead
of failing over. `fetch-sources.ps1` fetches them with several threads at once and checks
them against the hashes the ports declare, which is what makes the three hundred
megabytes of the Qt sources take minutes instead of an hour. Every thread resumes what
the previous attempt left behind, so a mirror that stalls costs a retry and not the
file.

`build-supervisor.ps1` watches a build that is already running, and when it sees such a
stall it finishes the download by hand, kills the processes that are stuck and restarts
the configure from the cache. It stops at the first stage that fails, so that the failure
can be dealt with instead of retried forever.

`build-status.ps1` is a snapshot of the build: what vcpkg is on, how much disk is left
and what the last lines of the log were.
