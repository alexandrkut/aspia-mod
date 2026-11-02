# Configures, builds and tests Aspia on Windows the way the CI does.
#
#   .\build-win.ps1 configure            # cmake --preset local-win-x64 (+ vcpkg dependencies)
#   .\build-win.ps1 build                # builds the project in the preset binary directory
#   .\build-win.ps1 test                 # ctest with --output-on-failure
#   .\build-win.ps1 clean                # drops vcpkg buildtrees (the largest reclaimable directory)
#   .\build-win.ps1 all
#
# Requires Visual Studio 2022 Build Tools (MSVC x14.44, Windows SDK) and CMake/Ninja.

param(
    [ValidateSet('configure', 'build', 'test', 'clean', 'all')]
    [string]$Target = 'all',

    [ValidateSet('local-win-x64', 'local-win-x86', 'ci')]
    [string]$Preset = 'local-win-x64',

    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'

$repo = Join-Path $PSScriptRoot 'aspia'
$repoPath = $repo -replace '\\', '/'
$buildDir = Join-Path $repo "builds\$Preset"

#--------------------------------------------------------------------------------------------------
# Toolchain

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found; Visual Studio Build Tools are not installed."
}

# Everything vswhere knows about is asked for and filtered below: several installations, or one
# that is registered oddly, is not worth failing over - the presence of the compiler decides.
$vcvars = & $vswhere -all -products * -property installationPath |
    ForEach-Object { Join-Path $_ 'VC\Auxiliary\Build\vcvars64.bat' } |
    Where-Object { Test-Path $_ } |
    Select-Object -First 1

if (-not $vcvars) {
    throw "No Visual Studio installation with vcvars64.bat was found."
}

$vsPath = Split-Path (Split-Path (Split-Path (Split-Path $vcvars)))
Write-Host "Visual Studio: $vsPath"

# winget installs CMake and Ninja outside the PATH this process was started with, and vcpkg needs
# both on PATH as well, so they are located explicitly and prepended to every command.
$toolDirs = @('C:\Program Files\CMake\bin')

$toolDirs += @(
    (Get-ChildItem (Join-Path $PSScriptRoot 'tools\bin') -Filter 'ninja.exe' -ErrorAction SilentlyContinue).FullName
    (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Links" -Filter 'ninja*' -ErrorAction SilentlyContinue).FullName
    (Get-ChildItem (Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake') -Filter 'ninja.exe' `
        -Recurse -ErrorAction SilentlyContinue).FullName
) | Where-Object { $_ } | ForEach-Object { Split-Path $_ }

$toolDirs = $toolDirs | Where-Object { Test-Path $_ } | Select-Object -Unique
foreach ($dir in $toolDirs) {
    Write-Host "Tool directory: $dir"
}

#--------------------------------------------------------------------------------------------------
# The environment every command runs in: the developer environment of the compiler (Ninja compiles
# through cl.exe, so it must be on PATH before cmake configures anything), the tools found above
# and the repository as the working directory (that is where the presets are).

function Invoke-InDevEnv([string[]]$Commands) {
    $vcvarsPath = $vcvars -replace '\\', '/'

    $lines = @("set `"PATH=$($toolDirs -join ';');%PATH%`"")
    $lines += $Commands

    $temp = Join-Path ([System.IO.Path]::GetTempPath()) ("aspia-{0}.cmd" -f [guid]::NewGuid())
    ($lines -join "`r`n") | Set-Content -Path $temp -Encoding ASCII

    & cmd.exe /c "`"$vcvarsPath`" >nul && `"$temp`""
    $code = $LASTEXITCODE
    Remove-Item $temp -ErrorAction SilentlyContinue

    if ($code -ne 0) {
        throw "Command failed with exit code $code"
    }
}

#--------------------------------------------------------------------------------------------------
# Space. Qt is built from source by vcpkg and is the bulk of the footprint: the installed tree, the
# object files left behind in buildtrees and the project build add up to tens of gigabytes.

$downloads = Join-Path $PSScriptRoot 'vcpkg-downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null

function Get-FreeGB {
    [math]::Round((Get-PSDrive C).Free / 1GB, 1)
}

function Invoke-Configure {
    $free = Get-FreeGB
    Write-Host "Free space on C: $free GB before configuring"
    if ($free -lt 25) {
        throw "Only $free GB left on C:. The dependency build needs 25-40 GB; free some space first."
    }

    $triplet = if ($Preset -eq 'local-win-x86') { 'x86-windows-static' } else { 'x64-windows-static' }
    $buildDirPath = $buildDir -replace '\\', '/'

    Invoke-InDevEnv @(
        "cd /d `"$repoPath`""
        "set `"VCPKG_TRIPLET=$triplet`""
        "set `"VCPKG_DEFAULT_HOST_TRIPLET=$triplet`""
        "set `"VCPKG_INSTALLED_DIR=$buildDirPath/vcpkg_installed`""
        "set `"VCPKG_DOWNLOADS=$($downloads -replace '\\', '/')`""
        'cmake --preset ' + $Preset
    )
}

# The project presets only define build/test presets for the CI configure preset, so the local ones
# are driven through their binary directory: the generator is multi-config, hence --config Release.
function Invoke-Build {
    $args = @('cmake --build "' + ($buildDir -replace '\\', '/') + '" --config Release')
    if ($Jobs -gt 0) {
        $args += "--parallel $Jobs"
    }
    Invoke-InDevEnv $args
}

function Invoke-Test {
    Invoke-InDevEnv @('ctest --test-dir "' + ($buildDir -replace '\\', '/') + '" -C Release --output-on-failure')
}

# The object files of the ports are only needed while they are built: once the installed tree
# exists, buildtrees is the largest reclaimable directory of the whole build.
function Clear-BuildTrees {
    $trees = Join-Path $repo 'vcpkg4aspia\buildtrees'
    if (Test-Path $trees) {
        Write-Host "Removing $trees"
        Remove-Item $trees -Recurse -Force
    }
}

switch ($Target) {
    'configure' { Invoke-Configure }
    'build' { Invoke-Build }
    'test' { Invoke-Test }
    'clean' { Clear-BuildTrees }
    'all' {
        Invoke-Configure
        Invoke-Build
        Invoke-Test
    }
}