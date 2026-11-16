param(
    [string]$ExePath = 'downloads/RazerBatteryTrayViperV2Pro.exe'
)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$version = (Get-Item -LiteralPath $exe).VersionInfo.FileVersion
if ($version -notmatch '^\d+\.\d+\.\d+$') {
    throw 'The EXE has no usable Windows file version.'
}

$log = Join-Path ([System.IO.Path]::GetTempPath()) "RazerBatteryTrayViperV2Pro-v$version-startup.log"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) "Razer Battery Tray $([Guid]::NewGuid())"
$null = New-Item -Path $temp -ItemType Directory
$copy = Join-Path $temp (Split-Path $exe -Leaf)
$program = $null
try {
    Copy-Item -LiteralPath $exe -Destination $copy
    Set-Content -LiteralPath $copy -Stream Zone.Identifier -Value "[ZoneTransfer]`r`nZoneId=3"
    if (Test-Path -LiteralPath $log) {
        Remove-Item -LiteralPath $log
    }

    $launch = [System.Diagnostics.ProcessStartInfo]::new($copy)
    $launch.UseShellExecute = $true
    $launch.Verb = 'open'
    $program = [System.Diagnostics.Process]::Start($launch)
    if (!$program) {
        throw 'Windows Shell did not return a tray process.'
    }

    for ($i = 0; $i -lt 50; $i++) {
        $program.Refresh()
        if ($program.HasExited) {
            throw ('Tray process exited with 0x{0:X8}.' -f $program.ExitCode)
        }
        if ($program.MainWindowHandle -ne [IntPtr]::Zero -and
            $program.MainWindowTitle -like "*v$version" -and
            (Test-Path -LiteralPath $log) -and
            (Select-String -LiteralPath $log -SimpleMatch 'Notification icon added' -Quiet)) {
            if ($program.WaitForExit(1000)) {
                throw ('Tray process exited with 0x{0:X8} after startup.' -f $program.ExitCode)
            }
            Write-Output $program.MainWindowTitle
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw 'The versioned status window and notification icon were not confirmed.'
} finally {
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log }
    if ($program -and !$program.HasExited) {
        Stop-Process -Id $program.Id
        $program.WaitForExit()
    }
    Remove-Item -LiteralPath $temp -Recurse -Force
}
