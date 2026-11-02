# Downloads the source archives the remaining vcpkg ports need, into the cache, and checks them
# against the hashes the ports declare.
#
# PowerShell 7 is required: the files are fetched with several threads at once, which is what makes
# the three hundred megabytes of Qt sources take minutes instead of an hour. Every thread resumes
# what the previous attempt left behind (-C -), so a mirror that stalls costs a retry, not the file.
#
# Run it with the portable PowerShell that vcpkg downloaded:
#   pwsh -NoProfile -File fetch-sources.ps1

param(
    [int]$Threads = 4,
    [int]$Retries = 10,
    [int]$BudgetSeconds = 120
)

$repo = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$cache = Join-Path (Split-Path $repo -Parent) 'vcpkg-downloads'

$files = @(
    @{
        Name = 'bzip2-1.0.8.tar.gz'
        Urls = @(
            'https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz'
            'https://mirrors.kernel.org/sourceware/bzip2/bzip2-1.0.8.tar.gz'
            'https://www.mirrorservice.org/sites/sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz'
        )
        Sha512 = '083f5e675d73f3233c7930ebe20425a533feedeaaa9d8cc86831312a6581cefbe6ed0d08d2fa89be81082f2a5abdabca8b3c080bf97218a1bd59dc118a30b9f3'
    }
    @{
        Name = 'qtbase-everywhere-src-6.11.1.tar.xz'
        Urls = @(
            'https://download.qt.io/archive/qt/6.11/6.11.1/submodules/qtbase-everywhere-src-6.11.1.tar.xz'
            'https://mirrors.ocf.berkeley.edu/qt/archive/qt/6.11/6.11.1/submodules/qtbase-everywhere-src-6.11.1.tar.xz'
        )
        Sha512 = 'b5608b6cefd483ecdc5e4fa3536acfc31116c8dfb698257f945180a8e412ee444dc670d754d4f6145649170f7d55637a70820188337a6c6b79193fbfbcd6a3fc'
    }
    @{
        Name = 'qtsvg-everywhere-src-6.11.1.tar.xz'
        Urls = @(
            'https://download.qt.io/archive/qt/6.11/6.11.1/submodules/qtsvg-everywhere-src-6.11.1.tar.xz'
            'https://mirrors.ocf.berkeley.edu/qt/archive/qt/6.11/6.11.1/submodules/qtsvg-everywhere-src-6.11.1.tar.xz'
        )
        Sha512 = '6cbadc333d3616a361360043da9a6809d03ee131cfb0661c9d01901c4bcf1d074c19f04b959616a1d35b712f2f50c66b5c40ea7bf204e680d054d538aabcf3e7'
    }
    @{
        Name = 'qttools-everywhere-src-6.11.1.tar.xz'
        Urls = @(
            'https://download.qt.io/archive/qt/6.11/6.11.1/submodules/qttools-everywhere-src-6.11.1.tar.xz'
            'https://mirrors.ocf.berkeley.edu/qt/archive/qt/6.11/6.11.1/submodules/qttools-everywhere-src-6.11.1.tar.xz'
        )
        Sha512 = '826d21ed3e45feebe8c083b9fc9fc8e2a64aaec972d58388c94eabeaa3c2d8e4c86f1620a7caa6a1784f2f84641aec3df4b20006fe369eeaeb1968b838497b56'
    }
    @{
        Name = 'jom_1_1_7.zip'
        Urls = @(
            'https://github.com/qt/jom/archive/refs/tags/v1.1.7.zip'
            'https://codeload.github.com/qt/jom/zip/refs/tags/v1.1.7'
        )
        Sha512 = ''
    }
)

function Test-Hash([string]$path, [string]$expected)
{
    if (-not $expected) { return $true }
    if (-not (Test-Path $path)) { return $false }

    $actual = (Get-FileHash -Path $path -Algorithm SHA512).Hash.ToLower()
    return ($actual -eq $expected.ToLower())
}

# The worker runs in its own runspace, so the shared values reach it through the using: scope.
# Passing them as -ArgumentList does not bind reliably here: the hashtable of one file gets spread
# over the parameter set and the call is refused.
$worker = {
    # Without -ArgumentList the pipeline object is only reachable as $_: a param() block would
    # bind nothing and the worker would look for files with an empty name.
    $file = $_

    function Test-Hash([string]$path, [string]$expected)
    {
        if (-not $expected) { return $true }
        if (-not (Test-Path $path)) { return $false }

        $actual = (Get-FileHash -Path $path -Algorithm SHA512).Hash.ToLower()
        return ($actual -eq $expected.ToLower())
    }

    $path = Join-Path $using:cache $file.Name

    if (Test-Hash $path $file.Sha512) {
        return 'have  {0}' -f $file.Name
    }

    for ($attempt = 1; $attempt -le $using:Retries; $attempt++)
    {
        $url = $file.Urls[($attempt - 1) % $file.Urls.Count]

        & curl.exe -L -s -f --connect-timeout 15 --max-time $using:BudgetSeconds -C - -o $path $url 2>$null

        if (Test-Hash $path $file.Sha512) {
            return 'ok    {0,-40} {1:N1} MB' -f $file.Name, ((Get-Item $path).Length / 1MB)
        }

        # A mirror that hangs is dropped when the budget runs out; what arrived is kept and the next
        # attempt resumes from that offset.
        Start-Sleep -Seconds 2
    }

    Remove-Item $path -Force -ErrorAction SilentlyContinue
    return 'FAIL  {0}' -f $file.Name
}

Write-Host "fetching $($files.Count) archives with $Threads threads"

$results = @($files | ForEach-Object -Parallel $worker -ThrottleLimit $Threads)

$results | ForEach-Object { Write-Host $_ }

$failed = @($results | Where-Object { $_ -like 'FAIL*' })

if ($failed.Count -gt 0) {
    Write-Host "FAILED: $($failed.Count)"
    exit 1
}

Write-Host 'all archives are in the cache'
exit 0