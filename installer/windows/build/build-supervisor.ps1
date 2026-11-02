# Supervises the Aspia build without a human in the loop.
#
# The dependency build (vcpkg) stalls from time to time: mirror.msys2.org accepts a connection and
# then never sends the body, and vcpkg waits forever instead of failing over. This script watches
# the log, and when it sees a stall it finishes the download by hand, kills the stuck processes and
# restarts the configure from the cache.
#
# After the dependencies are there it builds the project and runs the tests. It exits as soon as
# one of the three stages fails, so that the failure can be dealt with instead of retried forever.

param(
    [int]$StallMinutes = 8,
    [int]$MaxStalls = 12
)

$ErrorActionPreference = 'Stop'

$repo = Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent
$root = Split-Path $repo -Parent
$buildScript = Join-Path $PSScriptRoot 'build-win.ps1'
$downloads = Join-Path $root 'vcpkg-downloads'
$log = Join-Path $root 'build-supervisor.log'

function Write-Log([string]$message)
{
    $line = '{0} {1}' -f (Get-Date -Format 'HH:mm:ss'), $message
    Write-Host $line
    Add-Content -Path $log -Value $line
}

function Get-RunningProcess([string]$name)
{
    @(Get-Process -Name $name -ErrorAction SilentlyContinue)
}

#--------------------------------------------------------------------------------------------------
# Downloads one file of the vcpkg cache by hand. The name of the cached file is the name vcpkg
# prints, so a file it finds in the cache is not downloaded again.
function Save-Download([string]$name, [string]$url)
{
    $destination = Join-Path $downloads $name

    # The mirrors of msys2 answer the same files; a stalled one is replaced by its twin.
    $urls = @($url)
    if ($url -like '*mirror.msys2.org*') {
        $urls += $url.Replace('mirror.msys2.org', 'repo.msys2.org')
    }
    if ($url -like '*repo.msys2.org*') {
        $urls += $url.Replace('repo.msys2.org', 'mirror.msys2.org')
    }

    foreach ($candidate in $urls)
    {
        Write-Log "  downloading $name by hand from $candidate"

        & curl.exe -L --retry 5 --retry-delay 3 --retry-all-errors `
            --connect-timeout 15 --max-time 900 -s -f `
            -o $destination $candidate
        if ($LASTEXITCODE -eq 0 -and (Test-Path $destination) -and (Get-Item $destination).Length -gt 0) {
            Write-Log "  saved $((Get-Item $destination).Length) bytes"
            return $true
        }

        Write-Log "  curl failed with $LASTEXITCODE"
        Remove-Item $destination -Force -ErrorAction SilentlyContinue
    }

    return $false
}

#--------------------------------------------------------------------------------------------------
# What vcpkg was downloading when it stopped writing: the name of the cached file and the url.
function Get-PendingDownload([string]$logFile)
{
    $last = Select-String -Path $logFile -Pattern 'Downloading (.+?), trying (https?://\S+)' |
        Select-Object -Last 1

    if (-not $last) {
        return $null
    }

    # Only the download that has no "Successfully downloaded" behind it is the pending one.
    $after = (Get-Content $logFile | Select-Object -Skip $last.LineNumber)
    if ($after | Where-Object { $_ -match 'Successfully downloaded .+$($last.Matches[0].Groups[1].Value)' } |
            Select-Object -First 1) {
        return $null
    }

    return @{
        Name = $last.Matches[0].Groups[1].Value
        Url = $last.Matches[0].Groups[2].Value
    }
}

#--------------------------------------------------------------------------------------------------
# Runs one stage (configure, build, test) and returns what happened: 'ok', 'failed' or 'stalled'.
function Invoke-Stage([string]$target, [string]$stageLog)
{
    Write-Log "=== $target started"

    $process = Start-Process -FilePath 'powershell' `
        -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript,
                        '-Target', $target) `
        -RedirectStandardOutput $stageLog -RedirectStandardError "$stageLog.err" `
        -PassThru -WindowStyle Hidden

    $stalls = 0

    while (-not $process.HasExited)
    {
        Start-Sleep -Seconds 60

        if ($process.HasExited) {
            break
        }

        # Nothing written for a while while vcpkg is alive: it is waiting on a download.
        $idle = (Get-Date) - (Get-Item $stageLog).LastWriteTime

        if ($idle.TotalMinutes -lt $StallMinutes) {
            continue
        }

        if (-not (Get-RunningProcess 'vcpkg')) {
            Write-Log "$target is busy but no vcpkg is running; giving it more time"
            continue
        }

        $stalls++
        Write-Log "$target stalled for $([int]$idle.TotalMinutes) min (stall $stalls/$MaxStalls)"

        if ($stalls -gt $MaxStalls) {
            Write-Log 'too many stalls; giving up on this stage'
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
            Get-RunningProcess 'vcpkg' | Stop-Process -Force -ErrorAction SilentlyContinue
            Get-RunningProcess 'cmake' | Where-Object { $_.StartTime -gt $process.StartTime } |
                Stop-Process -Force -ErrorAction SilentlyContinue
            return 'stalled'
        }

        # Finish the download that is hanging, then start the stage over from the cache.
        $pending = Get-PendingDownload $stageLog

        if ($pending) {
            Save-Download -name $pending.Name -url $pending.Url | Out-Null
            Get-ChildItem $downloads -Filter '*.part' -ErrorAction SilentlyContinue |
                Remove-Item -Force -ErrorAction SilentlyContinue
        }
        else {
            Write-Log 'no pending download found; the stall is elsewhere'
        }

        Write-Log "restarting $target"

        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        Get-RunningProcess 'vcpkg' | Stop-Process -Force -ErrorAction SilentlyContinue
        Get-RunningProcess 'cmake' | Where-Object { $_.StartTime -gt $process.StartTime } |
            Stop-Process -Force -ErrorAction SilentlyContinue
        Get-RunningProcess 'ninja' | Stop-Process -Force -ErrorAction SilentlyContinue
        Get-RunningProcess 'cl' | Stop-Process -Force -ErrorAction SilentlyContinue

        Start-Sleep -Seconds 5

        $process = Start-Process -FilePath 'powershell' `
            -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript,
                            '-Target', $target) `
            -RedirectStandardOutput $stageLog -RedirectStandardError "$stageLog.err" `
            -PassThru -WindowStyle Hidden
    }

    $process.WaitForExit()

    if ($process.ExitCode -eq 0) {
        Write-Log "=== $target finished successfully"
        return 'ok'
    }

    Write-Log "=== $target FAILED with exit code $($process.ExitCode)"
    Get-Content $stageLog -Tail 25 -ErrorAction SilentlyContinue | ForEach-Object { Write-Log "  $_" }
    Get-Content "$stageLog.err" -Tail 15 -ErrorAction SilentlyContinue | ForEach-Object { Write-Log "  ! $_" }

    return 'failed'
}

#--------------------------------------------------------------------------------------------------

Set-Content -Path $log -Value "supervisor started $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"

foreach ($stage in @('configure', 'build', 'test'))
{
    $result = Invoke-Stage -target $stage -stageLog (Join-Path $root "stage-$stage.log")

    if ($result -ne 'ok') {
        Write-Log "supervisor stopped at stage '$stage' with result '$result'"
        Write-Log "free space: $([math]::Round((Get-PSDrive C).Free / 1GB, 1)) GB"
        exit 1
    }
}

Write-Log 'supervisor finished: configure, build and test all succeeded'
Write-Log "free space: $([math]::Round((Get-PSDrive C).Free / 1GB, 1)) GB"
exit 0