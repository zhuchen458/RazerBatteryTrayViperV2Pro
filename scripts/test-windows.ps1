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
$runKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Microsoft\Windows\CurrentVersion\Run')
$runName = 'RazerBatteryTrayViperV2Pro'
$previous = $runKey.GetValue($runName)

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TrayWindow {
    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr parent, int id);
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
}
'@

function Confirm-LogonToggle($window, $expectedCommand) {
    $id = 106
    $control = [TrayWindow]::GetDlgItem($window, $id)
    if ($control -eq [IntPtr]::Zero) { throw 'The logon checkbox is missing.' }
    if (![TrayWindow]::PostMessageW($window, 0x0111, [IntPtr]$id, $control)) {
        throw 'Could not send the checkbox click.'
    }

    for ($i = 0; $i -lt 20; $i++) {
        $actual = $runKey.GetValue($runName)
        if ($actual -ceq $expectedCommand) { return }
        Start-Sleep -Milliseconds 100
    }
    throw 'The logon checkbox did not update the Run key as expected.'
}

try {
    Copy-Item -LiteralPath $exe -Destination $copy
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
            $command = '"' + $copy + '" --background'
            Confirm-LogonToggle $program.MainWindowHandle $command
            Confirm-LogonToggle $program.MainWindowHandle $null
            Write-Output 'Logon checkbox enabled and disabled the expected startup command.'
            Stop-Process -Id $program.Id
            $program.WaitForExit()
            Remove-Item -LiteralPath $log
            $launch.Arguments = '--background'
            $program = [System.Diagnostics.Process]::Start($launch)

            for ($attempt = 0; $attempt -lt 50; $attempt++) {
                $program.Refresh()
                if ($program.HasExited) { throw 'The background process exited.' }
                if ((Test-Path -LiteralPath $log) -and
                    (Select-String -LiteralPath $log -SimpleMatch 'Notification icon added' -Quiet)) {
                    if ($program.MainWindowHandle -ne [IntPtr]::Zero -or
                        !(Select-String -LiteralPath $log -SimpleMatch 'Started in background mode' -Quiet)) {
                        throw 'Background startup unexpectedly showed a window.'
                    }
                    return
                }
                Start-Sleep -Milliseconds 200
            }
            throw 'Background startup did not add the tray icon.'
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
    if ($null -eq $previous) {
        $runKey.DeleteValue($runName, $false)
    } else {
        $runKey.SetValue($runName, $previous)
    }
    $runKey.Dispose()
    Remove-Item -LiteralPath $temp -Recurse -Force
}
