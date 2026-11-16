param(
    [string]$ExePath = 'downloads/RazerBatteryTrayViperV2Pro.exe'
)

$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $ExePath).Path
$log = Join-Path ([System.IO.Path]::GetTempPath()) 'RazerBatteryTrayViperV2Pro-startup.log'

function Wait-ForWindow($process) {
    for ($i = 0; $i -lt 50; $i++) {
        $process.Refresh()
        if ($process.HasExited) {
            throw ('Tray process exited with 0x{0:X8}.' -f $process.ExitCode)
        }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw 'The status window did not open.'
}

$program = Start-Process -FilePath $exe -PassThru
try {
    Wait-ForWindow $program
    for ($i = 0; $i -lt 20; $i++) {
        if ((Test-Path -LiteralPath $log) -and
            (Select-String -LiteralPath $log -SimpleMatch 'Status window shown' -Quiet)) {
            Write-Output $program.MainWindowTitle
            return
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'The startup log did not confirm that the status window was shown.'
} finally {
    if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log }
    if (!$program.HasExited) {
        Stop-Process -Id $program.Id
        $program.WaitForExit()
    }
}
