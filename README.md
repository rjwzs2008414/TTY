# TTY

跨平台串口通信封装，以及一个直接用它在 Windows / Linux 上发送 UART RGB
呼吸灯数据帧的示例。

作业联调协议与同学参考项目一致：

```text
115200 8N1, no flow control, 3.3 V TTL
A5 R G B CRC_L CRC_H
CRC-16/CCITT-FALSE over the first 4 bytes
```

## 目录

```text
include/TTY.h            串口封装（Windows + Linux）
include/rgb_protocol.h   RGB 帧、CRC-16、呼吸曲线、收包解码器
src/                    实现
examples/breath_led.cpp 发送端
tests/protocol_test.cpp 无需硬件的协议测试
```

## 编译

### 有 CMake

```powershell
cmake -S . -B build
cmake --build build --config Release
```

### 只有 MSYS2 MinGW（本机当前环境）

```powershell
New-Item -ItemType Directory -Force build | Out-Null
g++ -std=c++17 -O2 -Wall -Wextra -Iinclude `
    src/TTY.cpp src/rgb_protocol.cpp examples/breath_led.cpp `
    -o build/breath_led.exe
```

### Linux

```bash
cmake -S . -B build && cmake --build build
# 或
g++ -std=c++17 -O2 -Iinclude src/TTY.cpp src/rgb_protocol.cpp \
    examples/breath_led.cpp -o build/breath_led -pthread
```

## 运行

先做离线自测，不需要插板子：

```powershell
.\build\breath_led.exe --dry-run --seconds 2
```

输出里应能看到 `A5 00 00 00 F3 D2`（熄灭）、`A5 FF 00 00 90 1D`（红）等帧，
最后打印 `Stopped after ... frames`，2 秒 50 Hz 应为 100 帧左右。

找到串口并接好线后：

```powershell
.\build\breath_led.exe --list-ports
.\build\breath_led.exe --port COM5 --color 255,0,0 --hold
.\build\breath_led.exe --port COM5 --color 0,160,255 --seconds 20
```

常用参数：

| 参数 | 作用 |
|---|---|
| `--port COM5` | 串口名，Linux 例如 `/dev/ttyUSB0` |
| `--color 0,160,255` | 呼吸灯峰值颜色 |
| `--freq 0.5` | 呼吸频率，0.5 Hz = 2 秒一个周期 |
| `--fps 50` | 发送帧率 |
| `--seconds 20` | 20 秒后自动停止并发送熄灭帧 |
| `--hold` | 不呼吸，固定颜色 |
| `--test-colors` | 红绿蓝白灭每 2 秒轮播 |
| `--dry-run` | 不打开串口，只生成并打印帧 |
| `--quiet` | 不打印周期状态 |

## 接线

```text
视觉端 TX  ->  电控端 RX
视觉端 GND --- 电控端 GND
```

本协议没有回包，所以最低只需 TX 和 GND。必须共地。USB 转串口模块选
3.3 V TTL，不要把 RS-232 的 ±12 V 直接接到 STM32。

## 和电控联调顺序

1. 先让电控用串口助手十六进制发送 `A5 FF 00 00 90 1D`，确认灯变红。
2. 再依次测 `A5 00 FF 00 0C D1`（绿）、`A5 00 00 FF 03 CC`（蓝）、
   `A5 00 00 00 F3 D2`（灭）。
3. 电控确认 CRC、超时关灯都正常后，再运行本发送端。
4. 如果电控收到帧但灯色不对，先查 PWM 通道顺序、共阳/共阴极性和
   WS2812 的 GRB 顺序，不要先改串口协议。

## 关于原 TTY 封装

这个版本保留了 `TTY` 类名和 `SendData` / `ReceiveData` 的调用方式，但修掉了
原实现里会直接发错数据的问题：

- 原 `SendData` 把 CRC8 用 `memcpy(data, &crc, 1)` 写到了缓冲区开头，
  而不是末尾，并且只 `write` 了原长度；
- 原 `CalculateCrc8` 声明返回 `bool`，CRC 值被截断成 0/1；
- 原实现只有 Linux `termios`，Windows 不能编译；
- 原构造/读写失败直接 `exit()`，没有错误返回。

现在 `TTY` 只负责原样收发字节，不偷偷附加校验；RGB 协议需要的
CRC-16/CCITT-FALSE 放在 `rgb_protocol.h`，两端共用同一份帧定义。

## 参考

协议和联调步骤参考了同学作业项目
[streitwen/uart-rgb-breathing-light](https://github.com/streitwen/uart-rgb-breathing-light)
（MIT License）。本项目只复用了公开协议与思路，发送端代码为独立实现。