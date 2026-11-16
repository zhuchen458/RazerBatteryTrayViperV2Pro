# Razer Battery Tray · 毒蝰 V2 Pro

在 Windows 任务栏右下角直接显示雷蛇毒蝰 V2 Pro 的电量百分比。适用 Windows 10 / 11 x64，单个 EXE 双击即可运行，无需安装或管理员权限。

[下载 Windows x64 EXE](https://raw.githubusercontent.com/zhuchen458/RazerBatteryTrayViperV2Pro/main/downloads/RazerBatteryTrayViperV2Pro.exe) · [查看 GitHub Releases](https://github.com/zhuchen458/RazerBatteryTrayViperV2Pro/releases)

## 使用方法

1. 插上原装无线接收器，或用 USB 数据线连接鼠标，然后双击 EXE。
2. 在任务栏右下角找到电量图标。若藏在 `^` 菜单里，可以把它拖到任务栏通知区域。
3. 悬停查看百分比、连接方式和充电状态。右键可立即刷新、切换开机启动或退出。使用开机启动前，请先把 EXE 放到你打算长期保留的目录。

图标底部用绿色、黄色、红色显示电量档位，收到充电状态时显示青蓝色。默认每 30 秒查询一次。图标显示 `--` 时，悬停可查看当前状态及上次成功读数。鼠标休眠时可先移动鼠标，再点「立即刷新」。若鼠标已唤醒但始终无响应，可以退出 Razer Synapse 后再试，排查双方是否同时访问同一 HID 接口。

## 支持范围

| 连接方式 | USB VID:PID |
| --- | --- |
| 原装 2.4 GHz 接收器 | `1532:00A6` |
| USB 数据线 | `1532:00A5` |

两个端点均可查询时优先使用有线端点。单独出售的 HyperPolling 接收器使用其他设备 ID，本版仅匹配上表两个端点。

程序使用 Windows HID 接口查询电量和充电状态。它不更改 DPI、灯光或按键，不读取鼠标移动和点击数据，也不联网。开机启动只在当前用户的 Windows 注册表中写入启动路径，可从托盘菜单撤销。

## 从源码构建

在仓库根目录使用 Zig 0.15.2 或兼容版本。

```sh
zig cc -target x86_64-windows-gnu -std=c11 -Wall -Wextra -Werror -Os -s -Wl,--subsystem,windows src/tray.c src/device.c -lhid -lsetupapi -luser32 -lgdi32 -lshell32 -ladvapi32 -o RazerBatteryTrayViperV2Pro.exe
```

托盘交互参考 [InphicBatteryTray](https://github.com/Tan-TanZi/InphicBatteryTray)。雷蛇设备 ID 和电量查询协议参考 [OpenRazer](https://github.com/openrazer/openrazer) 与 [razer-battery-report](https://github.com/xzeldon/razer-battery-report)。
