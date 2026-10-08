# RA8D1 CAN-FD 端侧异常检测演示指南

本文给出两条演示路线：没有第二个 CAN 节点时使用板内合成流量；有 USB-CAN-FD 时完成真实物理总线验收。建议先完成离线演示，再接真实总线。

## 1. 演示目标

演示 RA8D1 在端侧完成以下闭环：

1. CAN/CAN-FD 报文采集；
2. 正常流量在线学习；
3. ID、周期、DLC、Payload 和全局帧率画像；
4. Unknown ID、Timing、DLC Change、Payload Jump、Flood 异常推理；
5. MIPI 五页 UI、串口诊断、事件日志和 LED 告警。

算法不依赖摄像头、网络、云端或动态内存。每个异常结果都包含分数和原因，便于解释与复现。

## 2. 演示前硬件检查

### 2.1 CANFD1 信号

扩展板已经带有 MCP2542FD-E/SN 收发器，不要再外接 TTL-CAN 收发器模块。

| 信号 | MCU 引脚 | J4 |
|---|---:|---:|
| CANFD1 RX / CRX1 | P208 | 3-4 |
| CANFD1 TX / CTX1 | P209 | 1-2 |
| CAN1 STB | P005 | 5-6 |

确认 J4 的三组横向跳帽均已安装。P005 在工程中输出低电平，使收发器处于正常工作状态。工程使用 SWD-only，下载调试仍由 P210/SWDIO 和 P211/SWCLK 完成。

### 2.2 CAN 接口和终端

真实总线按下表连接：

| 扩展板 CAN 接口 | USB-CAN-FD/第二节点 |
|---:|---|
| pin 5（CANH1） | CANH |
| pin 6（CANL1） | CANL |
| pin 3 或 pin 4（GND） | GND |

板上 R20/R21 各为 60.4 Ω，形成约 120.8 Ω 的固定分裂终端，不能通过跳帽关闭。因此本板放在总线一端，USB-CAN-FD 放在另一端并启用它的 120 Ω 终端。

断电后用万用表测 CANH-CANL：

- 只测本板：约 120 Ω；
- 两端终端均接好：约 60 Ω；
- 明显低于 60 Ω：终端可能过多；
- 明显高于 60 Ω：另一端终端、线束或连接可能缺失。

### 2.3 串口

SCI3 参数为 **115200-8-N-1，无流控**：

- USB 转串口 RXD → P409/TXD3；
- USB 转串口 TXD → P408/RXD3；
- GND → GND；
- VCC 不接。

## 3. 编译与烧录

参考环境：e² studio 2025-12、FSP 6.4.0、LLVM for ARM 21.1.1。

1. 在 e² studio 导入 `ra8d1_canfd_edge_ids`。
2. 打开 `configuration.xml`，确认目标器件为 `R7FA8D1BHECBD`。
3. 确认 CANFD1 为 500000 bit/s、数据段 2000000 bit/s，ISO 模式。
4. 点击 **Generate Project Content**。
5. 选择 `Debug` 并执行 **Build Project**。
6. 确认生成 `Debug/ra8d1_canfd_edge_ids.elf`。
7. 使用 `ra8d1_canfd_edge_ids Debug_Flat.launch` 下载并运行。

复位后预期看到：

```text
BOOT,RA8D1_CANFD_EDGE_IDS,DISPLAY,READY,CAN,READY,ERR,0,TIMEBASE,READY
```

如果 CAN 为 ERROR，先确认 J4、活动 Pin Configuration 和 CANFD Lite 生成内容；如果 DISPLAY 为 ERROR，检查屏幕排线和扩展板供电。

## 4. 路线 A：没有 USB-CAN-FD 的离线演示

这条路线不需要接 CANH/CANL，也不会因缺少 ACK 进入 Bus-Off。

1. 在串口输入 `h`，确认命令行工作。
2. 输入 `s`，串口应返回 `SELF_TEST,ON`，模型被清空并重新学习。
3. 前 **5 秒**只生成正常流量：
   - ID 0x100，周期 10 ms；
   - ID 0x180，周期 20 ms；
   - ID 0x240，周期 100 ms。
4. 正常阶段结束时模型自动冻结，状态由 LEARNING 变为 MONITORING。
5. 随后每种异常约持续 1.4 秒，并循环：
   - `UNKNOWN_ID`：插入未学习的 0x666；
   - `DLC_CHANGE`：0x100 的数据长度从 8 变为 4；
   - `PAYLOAD_JUMP`：0x100 的多个字节突变；
   - `FLOOD`：0x100 周期从 10 ms 加快到约 0.8 ms。
6. 输入 `u` 或短按 S1，依次观察 Dashboard、ID Profiles、Live Frames、Events、System Health。
7. 输入 `i` 检查模式、FPS、负载和控制器健康；输入 `e` 输出模型与最近事件。
8. 再次输入 `s` 关闭自测并清空模型，返回真实总线学习准备状态。

验收重点：四种异常原因均出现、分数变化、Events 有时间和 ID、P600 告警 LED 响应、页面切换不中断报文处理。

注意：该路线只验收算法、状态机、UI 和日志，不验收 CANFD1 外设、MCP2542FD、终端和线束。

## 5. 路线 B：USB-CAN-FD 真实总线演示

### 5.1 适配器设置

- 仲裁段：500 kbit/s；
- 数据段：2 Mbit/s；
- CAN-FD ISO；
- FD 报文启用 BRS；
- 本板与适配器共地；
- 适配器端启用 120 Ω 终端。

真实发送必须有第二节点 ACK。只有开发板一个节点时，不要在 Normal 模式持续发送。

### 5.2 安装 PC 端工具

```powershell
python -m pip install -r tools/requirements.txt
```

Linux SocketCAN 配置：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
sudo ip link set can0 up
```

发送 CAN-FD+BRS 流量：

```bash
python tools/canfd_demo_sender.py --interface socketcan --channel can0 --fd --attack cycle
```

Windows PCAN 示例：

```powershell
python tools/canfd_demo_sender.py --interface pcan --channel PCAN_USBBUS1 --fd --attack cycle
```

不同品牌适配器的 python-can 后端名和通道名不同，应按厂商驱动修改 `--interface` 和 `--channel`。发送 Classical CAN 时去掉 `--fd`。

脚本参数：

| 参数 | 作用 | 默认值 |
|---|---|---:|
| `--interface` | python-can 后端 | socketcan |
| `--channel` | 设备/通道 | can0 |
| `--bitrate` | 仲裁段速率 | 500000 |
| `--data-bitrate` | FD 数据段速率 | 2000000 |
| `--fd` | 发送 CAN-FD+BRS | 默认发送 Classical CAN |
| `--attack` | cycle/unknown/dlc/payload/flood | cycle |
| `--normal-seconds` | 正常流量时长 | 10 秒 |
| `--attack-seconds` | 单次攻击时长 | 2 秒 |

### 5.3 学习与攻击注入

1. 启动 PC 端正常流量前，在板端输入 `l`，避免旧模型影响结果。
2. 保证前 10 秒只有正常流量。真实模式自动完成学习必须同时满足：
   - 距离本次学习开始至少 10 秒；
   - 累计收到至少 100 帧。
3. 串口看到 `MODEL,MONITORING` 后再注入攻击。
4. 默认脚本在 10 秒正常流量后自动进入攻击阶段；若板端尚未 MONITORING，先停止脚本，输入 `l` 后重新开始。
5. 用 `--attack unknown`、`dlc`、`payload`、`flood` 可分别复测。
6. 输入 `i`，确认 TEC/REC 没有持续上升、没有 Bus-Off、接收队列没有持续 Drop。
7. 输入 `e` 保存 Profile 和 Event 串口输出，作为演示或作品集证据。

### 5.4 单板控制器回环

没有第二节点、但希望验证 CANFD Lite 的发送和回调时：

1. 输入 `o`，确认 `LOOPBACK,ON,ERR,0`；
2. 输入 `c` 发送一帧 Classical CAN 测试帧；
3. 输入 `f` 发送一帧 CAN-FD+BRS 测试帧；
4. 再次输入 `o`，退出内部回环后再接入真实总线。

内部回环仍不经过外部收发器和线束，不能替代真实总线验收。

## 6. 完整命令表

| 命令/操作 | 预期行为 |
|---|---|
| `l` | 清空模型、事件和统计，开始真实/当前输入源的正常流量学习 |
| `m` | 冻结现有模型进入 MONITORING；无帧时提示等待 |
| `s` | 开关离线合成攻击演示；切换时重新开始学习 |
| `o` | 开关 CAN 控制器内部回环 |
| `c` | 从 TX Buffer 0 发送一帧 Classical CAN 测试帧 |
| `f` | 从 TX Buffer 0 发送一帧 CAN-FD+BRS 测试帧 |
| `u` | 下一页 UI |
| `i` | 输出检测器和控制器诊断 |
| `e` | 输出全部已学习 Profile 和最近 IDS Event |
| `h` | 输出帮助 |
| S1 短按 | 下一页 UI |
| S1 长按 ≥0.8 秒 | 清空模型并重新学习 |

## 7. 五页 UI 验收

### Dashboard

- 状态能在 LEARNING、MONITORING、BUS ERROR 间正确切换；
- 显示 FPS、IDS 数量、TEC、REC、总线负载和异常分数；
- 异常标签与当前注入类型一致。

### ID Profiles

- 正常流量应学习到 0x100、0x180、0x240；
- DLC 和平均周期与发生器配置接近；
- 学习期间不应出现攻击 ID 0x666。

### Live Frames

- 报文持续刷新；
- ID、长度、帧标志和前 8 字节数据可见；
- 异常帧带对应原因。

### Events

- 最近异常包含时间、ID、分数和原因；
- 事件按新到旧显示；
- 相同异常有冷却时间，不会让界面被重复事件瞬间淹没。

### System Health

- 负载与风险曲线每秒采样一次，最多保留最近 48 秒；
- CAN Bus、Display、RX Queue、Controller 四个健康灯与串口诊断一致；
- Source 能区分真实 CAN 和离线合成演示，Loopback 能显示当前回环状态；
- Anomaly、Unknown、Drop、Bus-Off、TEC/REC 等计数与 `q`/`i` 输出一致。

## 8. 验收记录表

| 项目 | 通过条件 | 结果 |
|---|---|---|
| J4 | 1-2、3-4、5-6 均安装 | □ |
| 单板终端 | 断电 CANH-CANL 约 120 Ω | □ |
| 整网终端 | 两端连接后约 60 Ω | □ |
| 启动 | DISPLAY/CAN/TIMEBASE 均 READY | □ |
| 离线学习 | `s` 后约 5 秒进入 MONITORING | □ |
| Unknown ID | 0x666 被识别 | □ |
| Timing/Flood | 周期异常或全局洪泛被识别 | □ |
| DLC | 0x100 长度 8→4 被识别 | □ |
| Payload | 0x100 数据突变被识别 | □ |
| 真实学习 | ≥10 秒且 ≥100 帧后自动冻结 | □ |
| UI | 五页切换、趋势/健康灯和数据正确刷新 | □ |
| 诊断 | 无持续 Drop、TEC/REC 上升或 Bus-Off | □ |
| 回环 | `o` 后 `c`、`f` 返回 ERR 0 | □ |

## 9. 常见问题

### 一直停在 LEARNING

真实总线必须同时满足 10 秒和 100 帧。如果 FPS 为 0，检查 CANH/CANL、共地、J4、速率和适配器是否真正启动。也可输入 `i` 查看接收统计。

### 发送后 TEC 上升或 Bus-Off

常见原因是没有第二节点 ACK、位速率不一致、CANH/CANL 接反或终端不正确。停止发送，修正连接后复位控制器/开发板。单板测试改用 `o` 内部回环。

### 所有正常帧都被判断异常

可能在错误工况下学习、学习阶段混入了攻击、ID/DLC 已经变化，或 PC 脚本与板端学习起点没有对齐。输入 `l`，仅发送稳定正常流量至少 10 秒并确保超过 100 帧。

### Flood 时出现丢帧

接收 ISR 到主循环之间是固定 256 帧队列。极高总线负载、串口大量打印或 UI 刷新仍可能使队列溢出。用 `i` 查看 Drop；量产化时应继续按最坏帧率优化非阻塞日志并做压力测试。

### 断电电阻不是约 60 Ω

本板自身固定约 120.8 Ω。整网约 60 Ω 的前提是另一端也启用了一个 120 Ω，且测量时设备断电。不要给中间节点额外开启终端。

## 10. 工程限制与面试说明

- 这是在线统计异常检测，不是已经理解 DBC 信号含义的监督分类器，也不是功能安全产品。
- FDF/BRS/ESI 当前用于显示和负载估算，尚未进入异常画像；后续可加入“帧格式变化”特征与对应事件原因。
- 模型位于 RAM，复位后重学；当前最多 32 个 ID。
- 正常数据覆盖不足会误报，污染学习集会漏报，需要设计工况覆盖、模型持久化和版本管理。
- 固定 120.8 Ω 终端使开发板必须处于总线端点。
- 合成流量只证明算法链路；真实 USB-CAN-FD 测试才覆盖外设、ACK、收发器、线束和终端。

面试演示时可以重点说明：CAN ISR 与推理解耦、固定内存和确定性执行、Welford 在线统计、每 ID 行为画像、异常原因可解释、TEC/REC/Drop/Bus-Off 可观测，以及“离线自测—内部回环—真实总线”三层验证策略。
