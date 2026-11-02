# One progress snapshot of the Aspia build: what vcpkg is on, how much disk is left,
# what the last log lines were.
param(
    [string]$Log = ''
)

"=== $(Get-Date -Format 'HH:mm:ss') ==="

if (Test-Path $Log)
{
    $ports = Select-String -Path $Log -Pattern '^Installing (\d+)/(\d+) (.+?)\.\.\.' | Select-Object -Last 1
    if ($ports) { "ports: $($ports.Line -replace '^Installing ','  ' )" }
    else { "ports: (no Installing line yet)" }

    $errors = Select-String -Path $Log -Pattern 'error:|FAILED|fatal error' | Select-Object -Last 3
    if ($errors) { "errors:"; $errors | ForEach-Object { "  $($_.Line.Trim())" } }
    else { "errors: none" }

    "tail:"
    Get-Content $Log -Tail 3 | ForEach-Object { "  $_" }
}
else { "log not found: $Log" }

$repo = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$installed = Join-Path $repo 'builds\local-win-x64\vcpkg_installed\x64-windows-static'
"installed: $((Get-ChildItem "$installed\include" -Directory -ErrorAction SilentlyContinue).Count) headers, " +
    "$([math]::Round(((Get-ChildItem $installed -Recurse -File -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum / 1GB), 1)) GB"

$binary = Join-Path $repo 'builds\local-win-x64\Release\aspia-client.exe'
if (Test-Path $binary) { "client: BUILT ($(Get-Item $binary).Length bytes)" }
else { "client: not built yet" }

$tests = Join-Path $repo 'builds\local-win-x64\Release'
if (Test-Path $tests) { "release dir: $((Get-ChildItem $tests -Filter '*.exe' -ErrorAction SilentlyContinue).Name -join ', ')" }

"free: $([math]::Round((Get-PSDrive C).Free / 1GB, 1)) GB"
"build procs: $((Get-Process | Where-Object { $_.ProcessName -match 'cmake|vcpkg|ninja|^cl$|link$' }).ProcessName -join ', ')"