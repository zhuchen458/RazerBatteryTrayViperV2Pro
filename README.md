# Razer Battery Tray · 毒蝰 V2 Pro

在 Windows 任务栏右下角直接显示雷蛇毒蝰 V2 Pro 的电量百分比。适用 Windows 10 / 11 x64，单个 EXE 双击即可运行，无需安装或管理员权限。

[GitHub Releases](https://github.com/zhuchen458/RazerBatteryTrayViperV2Pro/releases) · [仓库备用下载](https://raw.githubusercontent.com/zhuchen458/RazerBatteryTrayViperV2Pro/main/downloads/RazerBatteryTrayViperV2Pro.exe)

## 启动排查

v1.0.4 双击先显示状态窗口，启动日志位于 `%TEMP%\RazerBatteryTrayViperV2Pro-v1.0.4-startup.log`。如果双击后没有窗口，请查看日志最后一行。若没有本次启动的日志，在 EXE 所在文件夹打开 PowerShell，运行下列命令并保留输出。

```powershell
try {
    $app = Start-Process -FilePath (Resolve-Path .\RazerBatteryTrayViperV2Pro.exe) -PassThru -ErrorAction Stop
    if ($app.WaitForExit(4000)) {
        'ExitCode = 0x{0:X8}' -f $app.ExitCode
    } else {
        'Process running, PID = {0}' -f $app.Id
    }
} catch {
    $_.Exception.Message
}
```

如果 Windows 安全中心的「保护历史记录」有与该 EXE 同一时间的拦截记录，也请保留这条记录。程序不需要管理员权限。

## 使用方法

1. 如果旧版还在后台，先在任务管理器结束 `RazerBatteryTrayViperV2Pro.exe` 再替换文件。双击新版会打开电量窗口，显示鼠标连接状态和托盘图标状态。
2. 先把 EXE 放到固定目录，再在窗口勾选「登录时自动启动」。以后登录 Windows 时，电量图标会自动出现在托盘。电脑已经运行时，可关掉电量窗口，继续在托盘查看。
3. 在任务栏右下角或 `^` 菜单中找到电量图标。左键打开电量窗口，关闭窗口后仍可在托盘查看电量。
4. 悬停可查看百分比、连接方式和充电状态。右键可立即刷新、切换开机启动或退出。

图标底部用绿色、黄色、红色显示电量档位，收到充电状态时显示青蓝色。默认每 30 秒查询一次。图标显示 `--` 时，悬停可查看当前状态及上次成功读数。鼠标休眠时可先移动鼠标，再点「立即刷新」。若鼠标已唤醒但始终无响应，可以退出 Razer Synapse 后再试，排查双方是否同时访问同一 HID 接口。

## 支持范围

| 连接方式 | USB VID:PID |
| --- | --- |
| 原装 2.4 GHz 接收器 | `1532:00A6` |
| USB 数据线 | `1532:00A5` |

两个端点均可查询时优先使用有线端点。单独出售的 HyperPolling 接收器使用其他设备 ID，本版仅匹配上表两个端点。

程序使用 Windows HID 接口查询电量和充电状态。它不更改 DPI、灯光或按键，不读取鼠标移动和点击数据，也不联网。开机启动只在当前用户的 Windows 注册表中写入启动路径，可取消窗口里的勾选，也可从托盘菜单关闭。

## 从源码构建

在仓库根目录使用 Zig 0.15.2 或兼容版本。

```sh
mkdir -p build downloads
(cd src && zig rc /fo ../build/application.res application.rc) || exit 1
for source in startup tray device memory; do
  zig cc -target x86_64-windows-gnu -std=c11 -Wall -Wextra -Werror -Os -fno-builtin \
    -c "src/$source.c" -o "build/$source.o" || exit 1
done
zig cc -target x86_64-windows-gnu -s -nostdlib \
  -Wl,--subsystem,windows -Wl,--entry=razor_start \
  build/startup.o build/tray.o build/device.o build/memory.o build/application.res \
  -lkernel32 -lhid -lsetupapi -luser32 -lgdi32 -lshell32 -ladvapi32 \
  -o downloads/RazerBatteryTrayViperV2Pro.exe
```

## 发布版本

在 Windows 上运行 `pwsh -File scripts/test-windows.ps1` 检查待发布的 EXE。对每个版本创建独立的 `vX.Y.Z` Release，将该版本的 `downloads/RazerBatteryTrayViperV2Pro.exe` 作为附件上传，并让版本标签指向包含该文件的提交。

托盘交互参考 [InphicBatteryTray](https://github.com/Tan-TanZi/InphicBatteryTray)。雷蛇设备 ID 和电量查询协议参考 [OpenRazer](https://github.com/openrazer/openrazer) 与 [razer-battery-report](https://github.com/xzeldon/razer-battery-report)。
