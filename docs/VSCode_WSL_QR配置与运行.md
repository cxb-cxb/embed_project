# VS Code + WSL + QSM368ZP-WF 配置与运行说明

更新时间：2026-07-02

## 1. 打开工程

在 VS Code 中使用 WSL 打开工程：

```text
/mnt/c/Users/cxb/Documents/嵌入式比赛相关的项目/Embed_project
```

建议安装 VS Code 扩展：

```text
WSL
C/C++
CMake Tools
```

## 2. 已配置的 VS Code 任务

按 `Ctrl + Shift + P`，选择：

```text
Tasks: Run Task
```

可用任务：

```text
WSL: install build tools
Build: native debug
Test: native
Build: ARM smart retail
Build: ARM QR scanner
Build: ARM QR scanner display
ADB: push project
ADB: run smart retail
ADB: run QR terminal preview
ADB: run camera LVDS preview
ADB: stop camera tasks
```

## 3. 推荐操作顺序

第一次配置时：

```text
WSL: install build tools
Test: native
ADB: push project
```

日常修改代码后：

```text
ADB: push project
```

运行智能零售命令行程序：

```text
ADB: run smart retail
```

运行二维码终端实时预览：

```text
ADB: run QR terminal preview
```

运行摄像头到 LVDS 屏幕预览：

```text
ADB: run camera LVDS preview
```

停止摄像头相关进程：

```text
ADB: stop camera tasks
```

## 4. 当前 QR 终端预览状态

当前 `qr_scanner` 已支持：

```text
-t  terminal preview with QR boxes
-p  preview refresh interval in frames
```

板端脚本：

```text
/userdata/Embed_project/scripts/qr_realtime_terminal.sh
```

实际执行：

```bash
./bin/qr_scanner -d /dev/video5 -W 800 -H 600 -t -p 5 "$@"
```

已验证结果：

```text
[camera] card    : rkisp_mainpath
[scanner] Terminal preview enabled. Refresh every 5 frame(s).
[preview] frame=... qr_found=0 boxes=0 size=800x600
```

说明：

- 终端可以实时显示摄像头灰度字符画。
- 检测到二维码时，会用 `#` 在终端字符画中框出二维码区域。
- 识别成功后会输出 `Payload`、`Version`、`ECC`、`Box`、`Corners`。
- 如果镜头前没有二维码，显示 `boxes=0` 是正常现象。

## 5. 当前 LVDS 状态

2026-07-04 已刷入屏幕修复固件中的 `boot.img` 和 `uboot.img` 后，LVDS 屏幕已恢复显示。

当前修复固件下的 DRM 参数为：

```text
LVDS connector id: 154
LVDS mode: 800x1280
可用 plane id: 73
输出格式: BGRx
```

摄像头到 LVDS 的 GStreamer 预览使用：

```bash
sh /userdata/Embed_project/scripts/camera_preview_lvds.sh
```

这个链路当前已验证可用。脚本内部会把摄像头 NV12 画面转换为 BGRx 后输出到 `plane-id=73`。

如果需要单独测试 LVDS 彩条，可在板端运行：

```bash
gst-launch-1.0 -v videotestsrc pattern=smpte is-live=true ! \
  video/x-raw,width=800,height=1280,framerate=30/1 ! \
  videoconvert ! video/x-raw,format=BGRx ! \
  kmssink driver-name=rockchip connector-id=154 plane-id=73 force-modesetting=true fullscreen=true sync=false
```

注意：旧固件中曾使用 `plane-id=89` 和 NV12 直出；修复固件下该组合会报 `failed to configure video mode`，应改用 `plane-id=73` 和 BGRx。

现阶段推荐：

- 比赛调试 QR：可以使用 `ADB: run QR terminal preview` 或电脑端预览工具。
- 展示摄像头画面：使用 `ADB: run camera LVDS preview`。
- LVDS 已恢复时，可继续使用 `qr_scanner_display` 做屏幕端零售展示。

注意：摄像头同一时间只能被一个程序占用。切换任务前先运行：

```text
ADB: stop camera tasks
```

## 6. ADB 路径

VS Code 任务默认使用：

```text
/mnt/c/tmp/RKDevTool_Release_v2.92/RKDevTool_Release_v2.92/bin/adb.exe
```

如果换电脑，需要修改：

```text
.vscode/tasks.json
```

或者在 WSL 终端中设置环境变量：

```bash
export ADB=/mnt/c/path/to/adb.exe
```
