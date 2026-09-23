[CmdletBinding()]
param(
    [string]$AdbPath = "",
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
$scriptDirectory = $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($scriptDirectory) -and
        -not [string]::IsNullOrWhiteSpace($MyInvocation.MyCommand.Path)) {
    $scriptDirectory = Split-Path $MyInvocation.MyCommand.Path -Parent
}
if ([string]::IsNullOrWhiteSpace($scriptDirectory)) {
    $scriptDirectory = (Get-Location).Path
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = $scriptDirectory
}

function Find-Adb {
    if ($AdbPath -and (Test-Path -LiteralPath $AdbPath -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $AdbPath).Path
    }

    $candidates = @(
        (Join-Path $scriptDirectory "adb.exe"),
        (Join-Path (Split-Path $scriptDirectory -Parent) "adb.exe"),
        "C:\platform-tools\adb.exe"
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command adb.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "adb.exe was not found. Pass -AdbPath C:\platform-tools\adb.exe"
}

function Save-AdbOutput {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string[]]$AdbArguments
    )

    $target = Join-Path $sessionDirectory $Name
    try {
        & $script:adb @AdbArguments 2>&1 |
            Out-File -LiteralPath $target -Encoding utf8 -Width 4096
    } catch {
        $_ | Out-File -LiteralPath $target -Encoding utf8 -Width 4096
    }
}

$script:adb = Find-Adb
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$sessionDirectory = Join-Path $OutputDirectory "AudioAshaDiagnostics-$stamp"
New-Item -ItemType Directory -Path $sessionDirectory | Out-Null

Save-AdbOutput -Name "00-adb-devices.txt" -AdbArguments @("devices", "-l")
Save-AdbOutput -Name "01-forward-list.txt" -AdbArguments @("forward", "--list")
Save-AdbOutput -Name "02-build-properties.txt" -AdbArguments @(
    "shell", "getprop"
)
Save-AdbOutput -Name "03-bluetooth-manager.txt" -AdbArguments @(
    "shell", "dumpsys", "bluetooth_manager"
)
Save-AdbOutput -Name "04-bluetooth-adapter-service.txt" -AdbArguments @(
    "shell", "dumpsys", "activity", "service",
    "com.android.bluetooth/.btservice.AdapterService"
)
Save-AdbOutput -Name "05-logcat-full.txt" -AdbArguments @(
    "logcat", "-d", "-v", "threadtime"
)

$senderCsv = Join-Path $scriptDirectory "AudioAshaSender-v1.7-last.csv"
if (Test-Path -LiteralPath $senderCsv -PathType Leaf) {
    Copy-Item -LiteralPath $senderCsv -Destination $sessionDirectory
}

$notes = @"
Audio-Asha v1.7 diagnostic snapshot
Created: $(Get-Date -Format o)

Use this immediately after hearing a glitch and after pressing Stop in Sender.
The sender CSV contains only anomalies and is buffered in memory during audio,
so diagnostic disk writes cannot cause the glitch.

Useful Android fields:
- source_pause_begin/end: Sender stopped transmitting during idle/starvation.
- pre-roll idle ticks are intentional skipped sends; no synthetic PCM packet
  was transmitted for them.
- AshaOS direct ADB audio / Input gap / PCM underflows: PC-to-phone path.
- Trigger/Packet dropped and send/flush: phone ASHA-to-L2CAP path.
- 'Credit:' and 'packets in channel' logcat lines: L2CAP congestion snapshots.
The current AshaOS build logs individual peer credits when its congestion/drop
path runs; it does not sample credits every 10 ms, because that would alter the
timing being measured.
"@
$notes | Out-File -LiteralPath (Join-Path $sessionDirectory "README.txt") -Encoding utf8 -Width 4096

$zipPath = "$sessionDirectory.zip"
Compress-Archive -LiteralPath $sessionDirectory -DestinationPath $zipPath
Write-Host "Diagnostic archive: $zipPath"