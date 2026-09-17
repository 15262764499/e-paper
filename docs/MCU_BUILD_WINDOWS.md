# MCU 固件：Pulse 接入与 Windows 编译

## 本次修改

USART1（PA9 TX、PA10 RX，经 CH340）接收 Pulse v1：115200、8N1、无流控。支持 HELLO、SET、STOP、CRC16、分包/粘包、非法参数拒绝及有界接收缓冲区。

- 第一个 SET 切入电脑灯效，Q2～Q6 每颗对应 20% 负载；Q1/Q7 关闭。
- 后续 SET 更新负载/亮度/速度，不重启动画；电脑端已经平滑数据。
- STOP 或超过 3 秒没有合法 SET 后停灯。HELLO 不刷新旧灯效有效期。
- NFC 接触期间保留全灯呼吸，结束后恢复仍有效的电脑灯效；已经过期则停灯。
- 按键和 BLE 本地模式选择会取消当前电脑灯效；如果电脑仍在发送 SET，下一包会重新接管。要长期使用本地模式，请先在 Pulse 断开连接。
- 原有湿度、扩散、BLE 图像和 OTA 路径保留。USART1 中断只入队，解析和应答在主循环/墨水屏等待服务中执行；断流保护和动画由 SysTick 独立执行。

源文件：`Core/Src/pulse_protocol.c`、`Core/Src/pulse_uart.c`、`Core/Inc/pulse_effect.h`；硬件驱动集成在 `Core/Src/led_595.c`。

## 这台电脑如何编译

打开 PowerShell：

```powershell
cd 'D:\文件\e-paper'
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-mcu.ps1
```

默认同时编译应用和 Bootloader，增量编译只重编修改的文件。全量重编：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-mcu.ps1 -Clean
```

只编译应用：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-mcu.ps1 -ApplicationOnly
```

已安装/检测到的工具：

- ARM GCC 14.3.1：`C:\Users\xyz\.local\arm-tools\bin\arm-none-eabi-gcc.exe`
- GNU Make：`C:\Program Files\mingw64\bin\mingw32-make.exe`
- 原来的 `gcc.exe` 是 Windows 本机编译器，不能替代 ARM GCC；本机没有名为 `make` 的命令，应使用 `mingw32-make`。

脚本自动查找 ARM GCC，仅临时修改当前编译进程的 PATH，不改系统 PATH。换电脑或换安装目录可以指定：

```powershell
powershell -ExecutionPolicy Bypass -File .\build-mcu.ps1 -ToolchainBin 'C:\工具链\bin'
```

也可以直接使用 Make：

```powershell
$env:PATH = 'C:\Users\xyz\.local\arm-tools\bin;' + $env:PATH
mingw32-make -j4 firmware
```

两个 Makefile 已兼容 Windows 建目录和清理命令，不再依赖 Unix `mkdir -p` / `rm`。

工具链来自 [Arm GNU 下载入口](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 对应的官方 Arm 存储：`arm-gnu-toolchain-14.3.rel1-mingw-w64-x86_64-arm-none-eabi.zip`。已对照官方 `.sha256asc` 校验：`864c0c8815857d68a1bbba2e5e2782255bb922845c71c97636004a3d74f60986`。

## 输出文件与烧录地址

| 内容 | HEX 文件（自带地址） | BIN 起始地址 |
|---|---|---|
| 应用 | `build/epaper_project.hex` | `0x08006000` |
| Bootloader | `build_bootloader/epaper_bootloader.hex` | `0x08000000` |

同目录还生成 `.bin`、`.elf`、`.map`。**不能把应用 BIN 写到 `0x08000000`**，它链接在 `0x08006000`，启动依赖前面的 Bootloader。

已有本工程 Bootloader 时一般只更新应用；空白芯片需要同时写入 Bootloader 和应用。曾经做过 OTA 的板子还要检查 `0x0801F800` 的 OTA 元数据：旧签名/哈希可能导致新应用被拒绝启动，请按现有 OTA 流程或明确的重新初始化流程处理，不要盲目整片擦除日历等持久数据。

烧录前先在 Pulse 中断开串口或从托盘退出，避免 COM4 被占用。Pulse 协议用于运行时控制，不能用于 STM32 ROM 下载；ROM 下载的校验、BOOT0/复位仍按下载工具要求设置。正常运行回到 BOOT0=0 后复位，打开 Pulse、选择 CH340 对应端口并连接。

## 验证结果和范围

已完成 ARM 编译、原生 C 协议/UART/LED 驱动回归测试，以及 43,200 组与电脑灯效的对照（整数舍入最多相差 1 个 duty 百分点）。应用 Flash 约 40 KB，小于 72 KB 应用分区；Bootloader 约 11 KB，小于 24 KB 分区。

GCC 14 链接器会对现有 `libnosys` 文件 I/O 空实现及链接脚本 RWX 段发出告警；构建成功且无 C 编译错误。这些告警不代表串口发送失败：本工程串口使用 HAL，不使用文件读写系统调用。

本次没有烧录或实板联调；PA9/PA10 焊接、CH340 实际通信、串口硬件错误恢复和灯的物理效果仍需板上验证。

原生测试（需要 PATH 中的 Windows `gcc`，不会打开串口）：

```powershell
New-Item -ItemType Directory -Force tmp | Out-Null
gcc -std=c11 -Wall -Wextra -Werror -ICore/Inc tests/test_pulse.c Core/Src/pulse_protocol.c -o tmp/test_pulse.exe
.\tmp\test_pulse.exe
gcc -std=c11 -Wall -Wextra -Werror -Itests/pulse_stubs -ICore/Inc tests/test_pulse_uart.c Core/Src/pulse_protocol.c -o tmp/test_pulse_uart.exe
.\tmp\test_pulse_uart.exe
gcc -std=c11 -Wall -Wextra -Werror -Itests/led_stubs -ICore/Inc tests/test_led_595.c -o tmp/test_led_595.exe
.\tmp\test_led_595.exe
C:\ProgramData\anaconda3\python.exe tests/test_led_protocol.py
C:\ProgramData\anaconda3\python.exe tests/test_pulse_effect.py
```
