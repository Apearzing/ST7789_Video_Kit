# hardware_spi_st7789 —— STM32F407 硬件 SPI1 驱动 ST7789 (240x240)

参照 `software_spi_st7789`（软件模拟 SPI，已点亮）移植，驱动 API 与软件版一一对应。

## 1. 接线对照（必须改 2 根线）

| 屏幕引脚 | 软件版接法 | 硬件版接法 | 说明 |
|---|---|---|---|
| CS | PA0 | PA0（不变） | 普通 GPIO，软件片选 |
| DC | PA1 | PA1（不变） | 普通 GPIO |
| RST | PA2 | PA2（不变） | 普通 GPIO |
| SCL | PA3 | **PA5** | 必须改到 SPI1_SCK |
| SDA/MOSI | PA4 | **PA7** | 必须改到 SPI1_MOSI |
| MISO | — | PA6（可不接） | SPI1 全双工主机模式配了 PA6，屏幕端一般没有 MISO，可悬空 |
| VCC/GND/BL | 原样 | 原样 | BL 可接 3V3 或 PWM |

> PA5/PA7 是 STM32F407 SPI1 的硬件复用引脚（AF5），改到别处硬件 SPI 不工作。

## 2. CubeMX 配置摘要（已写入 hardware_spi_st7789.ioc）

- MCU：STM32F407VET6，HSE 8MHz，SYSCLK 144MHz（PLLM=4, PLLN=144, PLLP=/2）
- APB2 = 72MHz，SPI1 分频 **/4 → SCK = 18MHz**（当前配置；全屏 240×240 刷新约 51ms）
  - 若点不亮或偶发花屏，退回 /8 = 9MHz 排查（`spi.c` 里 `SPI_BAUDRATEPRESCALER_8`）
- SPI1：Master，全双工，8bit，MSB，**Mode 3（CPOL=HIGH, CPHA=2EDGE）**，NSS=Software，CRC off
  - Mode 3 是关键：它复刻了软件版“SCK 空闲高、电平拉低→放数据→拉高锁存”的波形，配成 Mode 0 花屏/黑屏
- PA0/PA1/PA2：GPIO_Output，高速，初始 SET；PD8：LED
- PA5/PA6/PA7：SPI1 复用推挽，Very High 速度
- 时钟树、NVIC、SYS 与软件版一致

## 3. 打开与编译

1. 用 STM32CubeMX 打开 `hardware_spi_st7789.ioc`，确认无报错后点 **GENERATE CODE**
   （Code 目录的 `st7789_hspi.c/h` 放在 USER CODE 之外，不会被覆盖；main.c 的用户代码在 USER CODE 段内，也不会丢）
2. 用 Keil 打开 `MDK-ARM/hardware_spi_st7789.uvprojx`，Rebuild
   - 工程已包含：Core（含 spi.c）、HAL（含 stm32f4xx_hal_spi.c）、Code（含 st7789_hspi.c）、头路径 `../Code`
3. 烧录后现象：蓝屏 0.6s → 黑底 + 左上红块 + 右上绿块 + 白框 + 蓝块，PD8 以 0.5s 闪烁

## 4. 文件说明

- `Code/st7789_hspi.h/.c`：屏幕驱动，函数名与软件版完全一致（WriteCmd/WriteData/SetWindow/Clear/DrawPoint/DrawRect/DrawFillRect），初始化序列与软件版逐条相同（0x01/0x11/0x3A+0x55/0x36+0x00/0x21/0x29）
- `Core/Src/spi.c`：SPI1 初始化（Mode 3，/8 分频）
- `Core/Src/main.c`：演示程序
- 与软件版的移植差异仅在字节发送：GPIO 翻转 → `HAL_SPI_Transmit`；另 `Clear()` 改为按行整包发送（240 次调用，清屏更快）

## 5. 黑屏排查清单（按顺序查）

1. 接线：SCL 是否在 PA5、SDA 是否在 PA7（最常见错误）
2. SPI 模式：必须是 CPOL High + CPHA 2Edge（Mode 3），勿改 Mode 0
3. 波特率：默认 /4（18MHz）已稳定；若黑屏先退 /8（9MHz）定位问题
4. 屏幕供电/背光：VCC 3V3，BL 拉高
5. 复位延时：RST 拉低 20ms → 拉高 120ms（代码里已有，勿删）
6. 示波器/逻辑仪看 PA5 是否有 9MHz 时钟、PA7 是否有数据
7. 若颜色反了：初始化序列里的 0x21（INVON）注掉再试
8. 若画面整体上移/下移：调 DrawPoint/DrawFillRect 里的 +40 行偏置

## 6. 视频播放模式

`Code/video_player.c/h` 实现「串口收帧 → 校验 → 刷屏」的完整链路（UART1 1.5 Mbps + DMA 双缓冲）。
上电流程：方向卡 4 秒 → 黑屏等流 → 收到 `0x55 0xAA` 后开始播放，断流保持最后一帧。

协议格式、双缓冲实现、性能账本见仓库根目录：

- `README.md` —— 接线、上手、参数选择
- `docs/01-移植方案.md` —— 换 MCU / 换屏 / 换通道
- `docs/02-通信协议.md` —— 帧格式与 ACK 状态机