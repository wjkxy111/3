# CPKCOR-RA8D1B + CPKEXP-EKRA8X1：CANFD1 接线说明

本工程使用扩展板已经焊接的 `MCP2542FD-E/SN` 收发器，不要再串接一个 CAN 收发器模块。

## 为什么使用 CANFD1

MIPI 屏占用了核心板的 `P202/P203`（MIPI-DSI 时钟差分脚），所以扩展板的 CAN0 跳帽 J3 默认不安装。CANFD1 与 MIPI 屏可同时使用：

| 信号 | RA8D1 引脚 | 扩展板跳帽 |
|---|---:|---:|
| CRX1 | P208 | J4 3-4 |
| CTX1 | P209 | J4 1-2 |
| STB1 | P005 | J4 5-6 |

J4 的三只横向跳帽通常出厂已装。`STB1` 低电平为正常收发，高电平为待机；工程把 P005 配成 GPIO 输出低，板上 R19 还有 5.1 kΩ 下拉作为硬件兜底。

为了释放 P208/P209，调试接口从完整 JTAG 改成 SWD-only。下载调试仍使用 P210（SWDIO）和 P211（SWCLK）。

## 与 USB-CAN-FD 的连线

扩展板 2×3 `CAN` 插座使用以下针脚：

| 扩展板 CAN 插座 | 外部节点 |
|---:|---|
| pin 5：CANH1 | CANH |
| pin 6：CANL1 | CANL |
| pin 3 或 pin 4：GND | GND |

先设置：

- Nominal bitrate：500 kbit/s
- Data bitrate：2 Mbit/s
- CAN-FD：ISO 模式
- CAN-FD 帧使用 BRS 时才进入 2 Mbit/s 数据阶段

确认扩展板 `VIN_5V` 有电；收发器总线侧使用 5 V，MCU 逻辑侧 VIO 为 3.3 V。

## 终端电阻

CANFD1 已固定安装分裂终端：R20/R21 各 60.4 Ω，中点通过 C8 22 pF 到地，CANH1-CANL1 等效约 120.8 Ω。板上没有画终端使能跳线，因此本板应放在总线一端，另一端再使能一个 120 Ω 终端。

断电测 CANH-CANL：

- 只连接本板一个终端：约 120 Ω
- 总线两端各一个 120 Ω：约 60 Ω
- 明显低于 60 Ω：终端过多

## 没有第二个 CAN 节点时

真实总线发送必须收到其他节点的 ACK。只有一块板直接在 Normal 模式发送，会不断产生 ACK Error，并可能进入 Bus-Off。可用以下两种离线演示方式：

1. 串口命令 `s`：完全不访问物理总线的合成流量，依次演示 Unknown ID、DLC Change、Payload Jump 和 Flood。
2. 串口命令 `o`：切换控制器内部回环，再用 `c` 或 `f` 发送测试帧。

接入真实总线前，再用 `o` 退出内部回环。

## 原理图依据

- [扩展板原理图](./CPKEXP-EKRA8X1_V1_SCH.pdf)：第 6/9 页为两路 CAN-FD 收发器、J3/J4、外部插座和分裂终端。
- [核心板原理图](./CPKCOR_RA8x1x_V2_schematic_release.pdf)：第 2/11 页可看到 P202/P203 的 MIPI-DSI 复用关系。

