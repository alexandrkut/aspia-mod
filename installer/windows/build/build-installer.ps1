<#
.SYNOPSIS
    Builds the Windows MSI of the client or of the router.

.DESCRIPTION
    Compiles installer/windows/<product>.wxs into an MSI holding the program that was built by
    build-win.ps1. Only en-us: the other languages of the original need wisubstg.vbs, which is
    not in this Windows Kits.

    The router's installer takes the service along with the binary - it runs --install as a
    deferred action, so the service is created by the package rather than by the person running
    it. That is the reason it needs the rights the deferred action is given, and why the binary
    has to be in the package for the action to find.

.EXAMPLE
    .\build-installer.ps1
    .\build-installer.ps1 -Product router
    .\build-installer.ps1 -Product router -Version 3.1.1.1
#>
param(
    [ValidateSet('client', 'host', 'router', 'relay')]
    [string]$Product = 'client',
    [string]$Version = '',
    [string]$Architecture = 'x86_64',
    [string]$Language = ''
)

$ErrorActionPreference = 'Stop'

$root   = 'C:\Projects\aspia-mod'
$wix    = Join-Path $root 'tools\wix3'
$srcDir = Join-Path $root "aspia\installer\windows"
$build  = Join-Path $root 'aspia\builds\local-win-x64\Release'
$stage  = Join-Path $root "aspia\builds\msi-$Product-$Architecture"

# The name of the program in the list of the installed programs and the name of the shortcut in
# the menu "Start" come from the translation of the package. The client and the host of the mod
# are in Russian, like the original ones, and the router and the relay stay in English: the
# translations of the original for them are in English too.
if (-not $Language)
{
    $Language = if ($Product -in @('client', 'host')) { 'ru-ru' } else { 'en-us' }
}

# The language of the package is the language of its translation, and the installer refuses a
# package whose language does not match the language of the translation.
$languageId = if ($Language -eq 'ru-ru') { '1049' } else { '1033' }

# The version of the package is the version of the program: the three numbers of the sources
# and the number of the build of this line of the versions, which is the fourth number of the
# version the program shows in the window "About". The installer replaces a package of the
# same version, so the number is raised by hand with every release.
if (-not $Version)
{
    $cmake = Get-Content (Join-Path $root 'aspia\CMakeLists.txt') -Raw

    $parts = @('MAJOR', 'MINOR', 'PATCH', 'BUILD') | ForEach-Object {
        if ($cmake -match "set\(ASPIA_VERSION_$_ (\d+)\)") { $Matches[1] } else { throw "the version is not in the sources" }
    }

    $Version = $parts -join '.'

    Write-Host "the version of the package is $Version"
}

# The mark of a modified build goes into the name of the file and not into the version of the
# package: the version of the package is a number, and the installer compares it with the
# version of the package that is installed.
$cmake = Get-Content (Join-Path $root 'aspia\CMakeLists.txt') -Raw
if ($cmake -match 'set\(ASPIA_VERSION_SUFFIX "([^"]*)"\)') { $Suffix = $Matches[1] } else { $Suffix = '' }

$executable = "aspia_$Product.exe"
$builtBinary = Join-Path $build $executable

if (-not (Test-Path $builtBinary))
{
    throw "The $Product is not built: $builtBinary is missing. Run build-win.ps1 -Target build first."
}

# The host takes the credentials of the user with it in a library of its own, and the
# installer puts it next to the program.
$extraFiles = @()

if ($Product -eq 'host')
{
    $credentials = Join-Path $build 'aspia_credentials.dll'

    if (-not (Test-Path $credentials))
    {
        throw "The credentials library is not built: $credentials is missing."
    }

    $extraFiles += $credentials
}

foreach ($tool in 'candle.exe', 'light.exe')
{
    if (-not (Test-Path (Join-Path $wix $tool)))
    {
        throw "WiX is not unpacked: $tool is missing under $wix"
    }
}

# The staging folder holds the files the installer takes. A folder of its own keeps the
# .wixobj and the .cab out of the build tree of the program itself, and keeps the products
# from overwriting each other's leftovers.
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $builtBinary $stage -Force

foreach ($file in $extraFiles)
{
    Copy-Item $file $stage -Force
}

$env:ASPIA_VERSION  = $Version
$env:ASPIA_LANGUAGE = $languageId
$env:ASPIA_BIN_DIR  = $stage

$candleArch = switch ($Architecture) {
    'x64'     { 'x64' }
    'x86_64'  { 'x64' }
    'x86'     { 'x86' }
    default   { throw "Architecture not known to candle: $Architecture" }
}

$extensions = @(
    '-ext', 'WixUtilExtension',
    '-ext', 'WixUIExtension',
    '-ext', 'WixFirewallExtension'
)

$msi = Join-Path $stage "aspia-$Product-$Version$Suffix-$Architecture.msi"
$wxs = "$Product.wxs"
$wxl = "translations\$Product.$Language.wxl"

Push-Location $srcDir
try
{
    Write-Host '##################################################'
    Write-Host "Aspia $Product MSI"
    Write-Host "  Version    : $Version"
    Write-Host "  Language   : $Language ($languageId)"
    Write-Host "  Arch       : $Architecture (candle: $candleArch)"
    Write-Host "  Exe        : $builtBinary"
    Write-Host "  Output     : $msi"

    & (Join-Path $wix 'candle.exe') -out "$stage\" -arch $candleArch @extensions $wxs
    if ($LASTEXITCODE -ne 0) { throw "candle failed with $LASTEXITCODE" }

    # The cultures are loaded in the order of the precedence: the strings of the translation of
    # the package win, and the English ones fill the gaps: the extensions of the installer have
    # no translation in Russian.
    & (Join-Path $wix 'light.exe') -sval -out $msi -cultures:"$Language;en-us" @extensions -loc $wxl `
        "$stage\$Product.wixobj"
    if ($LASTEXITCODE -ne 0) { throw "light failed with $LASTEXITCODE" }

    # The version in the list of the installed programs shows the mark of a modified build too,
    # so that the packages of the mod are told from the packages of the original. The builder of
    # the package takes numbers only in the version, so the mark is written into the package after
    # it is built. The installer compares the versions by the first three numbers, and the mark is
    # in the fourth one, so the comparison of the versions is not affected by it.
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $database = $installer.OpenDatabase($msi, 1)
    $view = $database.OpenView("UPDATE ``Property`` SET ``Value``='$Version$Suffix' WHERE ``Property``='ProductVersion'")
    $view.Execute()
    $view.Close()
    $database.Commit()
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($database) | Out-Null
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null
}
finally
{
    Pop-Location
}

Write-Host '##################################################'
Get-Item $msi | Select-Object Name, @{n='MB'; e={ [math]::Round($_.Length / 1MB, 2) } }, LastWriteTime