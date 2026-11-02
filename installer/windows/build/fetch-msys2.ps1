# Downloads every msys2 package the Qt ports need into the vcpkg cache.
#
# mirror.msys2.org accepts connections and then stops sending the body, which is what the build
# kept hanging on. A file that is already in the cache is never fetched again by vcpkg, so doing
# the whole list up front - with short timeouts and a few retries - keeps the stall out of the
# build entirely.

param(
    [int]$Throttle = 4,
    [int]$Retries = 6
)

$repo = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$root = Split-Path $repo -Parent
$cache = Join-Path $root 'vcpkg-downloads'
$port = Join-Path $root 'aspia\vcpkg4aspia\scripts\cmake\vcpkg_acquire_msys.cmake'

$urls = Select-String -Path $port -Pattern 'URL "https://mirror\.msys2\.org/([^"]+)"' -AllMatches |
    ForEach-Object { $_.Matches } |
    ForEach-Object { 'https://mirror.msys2.org/' + $_.Groups[1].Value } |
    Sort-Object -Unique

$todo = @()
foreach ($url in $urls)
{
    $name = 'msys2-' + (($url -replace '^.*/', '') -replace '^', '')
    $path = Join-Path $cache $name

    if (Test-Path $path) { continue }

    $todo += [pscustomobject]@{ Name = $name; Url = $url; Path = $path }
}

Write-Host "in list: $($urls.Count), missing: $($todo.Count)"

if ($todo.Count -eq 0) {
    Write-Host 'nothing to download'
    exit 0
}

# A file is fetched with short timeouts so that a mirror that hangs is dropped in a minute instead
# of an hour, and from the twin mirror if the first one misbehaves. Windows PowerShell has no
# parallel pipeline, so the files are walked one at a time - they are small and most of them come
# back in well under a second when the mirror is healthy.

function Fetch-One($item)
{
    $mirrors = @($item.Url)
    if ($item.Url -like '*mirror.msys2.org*') {
        $mirrors += $item.Url.Replace('mirror.msys2.org', 'repo.msys2.org')
    }

    foreach ($mirror in $mirrors)
    {
        for ($attempt = 1; $attempt -le $Retries; $attempt++)
        {
            & curl.exe -L -s -f --connect-timeout 10 --max-time 90 -o $item.Path $mirror 2>$null

            if ($LASTEXITCODE -eq 0 -and (Test-Path $item.Path) -and (Get-Item $item.Path).Length -gt 0)
            {
                return 'OK   {0} [{1} KB]' -f $item.Name, [math]::Round((Get-Item $item.Path).Length / 1KB)
            }

            Remove-Item $item.Path -Force -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 2
        }
    }

    return "FAIL $($item.Name)"
}

$failed = @()

foreach ($item in $todo)
{
    $line = Fetch-One $item
    Write-Host $line

    if ($line -like 'FAIL*') { $failed += $item.Name }
}

Write-Host "done, failed: $($failed.Count)"
if ($failed.Count -gt 0) { $failed | ForEach-Object { Write-Host "  $_" } }

exit ($(if ($failed.Count -eq 0) { 0 } else { 1 }))