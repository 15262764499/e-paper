# Pulse · Windows 电脑负载灯效

原生 Windows 桌面程序。采集 CPU、NVIDIA GPU、物理内存占用，预览 74HC595 / Q2～Q6 的流水呼吸效果，并通过 CH340 发送 Pulse v1 控制协议。

## 直接运行

打开 `dist/Pulse/Pulse.exe`。程序已包含 Python 和 Qt 运行库，不需要另外安装 Python。

**请保留完整的 `Pulse` 文件夹（包括 `_internal`），不要只复制 exe。** 首次启动若 Windows 对未签名应用显示提示，可核对来源后选择运行。本项目没有购买代码签名证书。

1. 点击 CPU、GPU 或 RAM 卡片，选择映射指标。右侧可调亮度、呼吸速度。
2. 无开发板时，真实硬件监控和本地灯效预览仍可使用。
3. 接入 USB，选择 CH340 串口（自动优先列出 WCH 设备），点击“连接开发板”。
4. “开发板已连接”表示收到 Pulse v1 固件的握手应答；只有打开串口不算连接成功。
5. “固件未应答”表示串口已打开，但板端未回应协议。**原来的湿度灯效固件不能直接接收电脑负载数据。2026-09-17 已增加 MCU 协议支持，需编译并烧录新版 MCU 固件，见 `../docs/MCU_BUILD_WINDOWS.md`。**
6. 暂停灯效会发送 STOP，实时监控继续。默认关闭窗口后驻留托盘，采集和串口发送继续。托盘菜单“退出 Pulse”或断开连接会尝试 STOP 并释放串口；固件仍需实现 3 秒超时停灯作为保障。

## 后台运行（v1.1）

- 点击窗口关闭按钮后隐藏到系统托盘；单击托盘图标恢复窗口，右键可暂停/恢复灯效、打开后台设置或退出。
- 左侧“后台设置”可关闭托盘驻留，也可选择手动启动时隐藏窗口。
- “登录 Windows 后静默启动”默认关闭；开启后写入当前用户 Run 启动项，无需管理员权限。关闭选项会删除 Pulse 的启动项。
- 请先将完整程序文件夹放在固定位置，再开启自启动。移动文件夹后需重新开启此选项。
- 启动时不会自动连接串口。已经连接的程序隐藏后继续通信；重新启动或登录后需从托盘恢复窗口并连接。
- 隐藏时界面动画降低刷新频率，后台数据采集和串口更新仍为每 500 ms。没有可用系统托盘时显示窗口，避免无法找回程序。
- 命令行参数 `--start-hidden` 可直接静默启动。这是用户登录后的桌面后台程序，不是 Windows 服务。

## 指标与灯效

- CPU：psutil 采样间隔内的总体利用率；第一份数据在预采样后产生。
- RAM：`psutil.virtual_memory().percent`，详情为总量减 available 的使用量。
- GPU：NVIDIA NVML 的 GPU 核心利用率；支持多个 NVIDIA GPU 下拉选择，**Intel/AMD GPU 首版未支持**。指标可能与任务管理器不同；GPU 不可用时显示 `—` 并停止该指标的设备输出，不伪造 0%。
- 曲线和卡片显示原始采样，灯效映射用 alpha=0.35 的指数平滑。数据每 500 ms 更新，预览约 30 FPS，板端建议 10 ms 更新动画。
- Q2～Q6 每颗代表 20%；Q1、Q7 保持关闭。单颗呼吸 1200 ms，相邻延后 300 ms，周期 3000 ms。100% 负载全亮；Q2 峰值上限为 80%，保留原板亮度限制。整体亮度再统一缩放。
- 现有硬件是多颗固定颜色的单色灯，预览色彩为示意，不是每颗可任意变色的 RGB 像素。
- 程序不上传采样数据，不需要账号；偏好存储于 Windows 当前用户的 `HKEY_CURRENT_USER\Software\EPaper\Pulse`。

## 串口注意事项

- 115200、8N1、无软/硬件流控。它与 STM32 ROM 烧录协议的偶校验设置不同。
- 启动程序不会自动打开串口；点击连接后才打开。打开前将 RTS、DTR 设为 false（解除断言），避免主动执行下载复位序列。驱动在打开端口瞬间是否有脉冲仍需实物验证。
- 自动重连每 3 秒尝试**原串口号**；拔插后端口号变化需要手动选择。程序不会自动改连其他设备。
- 用其他工具烧录或调试前先断开 Pulse；Windows 串口通常是独占的。
- “连接记录”可查看及复制错误。通信错误、旧固件无应答、设备拒绝命令都有独立状态。

## 从源码运行

推荐 Python 3.13 x64。在本目录打开 PowerShell：

```powershell
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r requirements.txt
.venv/Scripts/python.exe main.py
```

也可以双击 `run.cmd`（要求上述虚拟环境已安装完成）。源码目录是 `pulse/`。

## 构建与验证

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

或者直接测试：

```powershell
.venv/Scripts/python.exe -m pytest tests -q
.venv/Scripts/python.exe main.py --smoke-test --screenshot qa/dashboard.png
```

冒烟测试采集真实数据，不会打开串口，7 秒后保存截图和同名 JSON 并退出。`--size 1120x760` 验证小窗口；`--metric memory` 选择内存预览。所有资源通过 Qt 原生绘制；正常运行使用 Windows 系统字体。

## 文件结构

- `pulse/telemetry.py`：CPU、RAM、NVML 采集。
- `pulse/protocol.py`：二进制封包、CRC 与增量解析。
- `pulse/device.py`：握手、应答、重连和端口生命周期。
- `pulse/workers.py`：后台采集及串口线程。
- `pulse/effect.py`：灯效及平滑计算。
- `pulse/ui.py`、`pulse/widgets.py`：界面和原生图形组件。
- `PROTOCOL.md`：固件接入规范与校验向量。

第三方组件：PySide6/Qt（LGPLv3/GPLv3/commercial）、psutil（BSD-3-Clause）、pyserial（BSD-3-Clause）、nvidia-ml-py（BSD-3-Clause）、PyInstaller（GPL-2.0-or-later with bootloader exception）。分发包包含所用组件可获取的许可证；后续对外发布请保留这些文件。
