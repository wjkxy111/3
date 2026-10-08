# RA8D1 CAN-FD 端侧异常检测

## 板端可靠性更新（2026-10-09）

检测配置中的 NaN、正负无穷会回退到默认值，避免周期、Payload 或洪泛检测
因非有限参数失效。重新初始化时可以安全传入 `&detector.config`，原有有效参数
会保留，学习画像与统计计数仍会清空。

运行 `python tools/run_ai_tests.py` 可用 GCC 检查非有限参数、自配置重置、正常学习、
周期过快、Payload 突变、未知 ID 和非法 ID。支持 `--cc <编译器路径>`。
本次还通过既有 AUTOSAR/Bootloader 主机测试及 Cortex-M85 编译检查；固件源码需要
在 e² studio 重新构建并下载后才会在开发板生效，本次未进行实板烧录。

> 显示硬件说明：当前 H0233S001 是 222×480 的非触摸屏；工程使用 256 像素
> 帧缓冲跨度满足 GLCDC 对齐。触摸能力、预留管脚和替换条件见
> [docs/DISPLAY_TOUCH.md](docs/DISPLAY_TOUCH.md)。

这是面向 **CPKCOR-RA8D1B + CPKEXP-EKRA8X1** 的独立端侧 AI 工程。RA8D1 在本地学习正常 CAN 流量，不依赖摄像头、网络或云服务，并在运行阶段检测以下异常：

- 未学习的 CAN ID（Unknown ID）
- 报文周期过快或过慢（Timing）
- DLC/数据长度变化（DLC Change）
- 数据字节偏离正常分布（Payload Jump）
- 全局报文速率异常升高（Flood）

检测器会为每个 CAN ID 建立周期、DLC 和逐字节统计特征，同时学习总线帧率。结果包含 0～1000 的异常分数和可解释的原因位，适合演示“采集—在线学习—端侧推理—告警—可视化”的完整嵌入式 AI 链路。

详细演示步骤见 [docs/DEMO_GUIDE.md](docs/DEMO_GUIDE.md)，硬件依据和接线说明见 [docs/hardware/CAN1_WIRING.md](docs/hardware/CAN1_WIRING.md)。

## 主要功能

- RA8D1 `CANFD1`，FSP `r_canfdlite` 驱动，ISO CAN-FD。
- 仲裁段 500 kbit/s，CAN-FD 数据段 2 Mbit/s，支持 BRS。
- 中断只复制报文到固定队列，学习、推理、日志和 UI 均在主循环执行。
- 最多学习 32 个 ID；支持标准帧、扩展帧、Classical CAN 和最多 64 字节的 CAN-FD 帧。
- 256 帧接收队列、16 条异常事件环形日志，全程无动态内存分配。
- 256×480 MIPI 屏五页界面：总览、ID 画像、实时报文、告警事件、系统健康。
- 屏幕使用中英双语高对比标签、48 秒负载/风险趋势、健康灯和生命周期计数器；固定内存、无动态分配。
- SCI3 115200-8-N-1 命令行、控制器状态、TEC/REC、丢帧和 Bus-Off 诊断。
- 不接 CAN 设备也能运行的合成流量异常演示。

## 板载 CAN-FD 硬件

扩展板已经安装 `MCP2542FD-E/SN` 收发器，不需要再串接外部 CAN 收发器模块。本工程使用可与 MIPI 屏同时工作的 CANFD1：

| 信号 | RA8D1 引脚 | J4 跳帽 | 说明 |
|---|---:|---:|---|
| CRX1 | P208 | 3-4 | CANFD1 接收 |
| CTX1 | P209 | 1-2 | CANFD1 发送 |
| STB1 | P005 | 5-6 | 低电平为正常收发 |

J4 的三只横向跳帽通常已出厂安装，使用前仍应逐一确认。为了释放 P208/P209，工程使用 SWD-only 调试；P210 为 SWDIO，P211 为 SWCLK。

扩展板 2×3 `CAN` 接口连接 USB-CAN-FD 或另一节点：

| CAN 接口 | 外部设备 |
|---:|---|
| pin 5（CANH1） | CANH |
| pin 6（CANL1） | CANL |
| pin 3 或 pin 4（GND） | GND |

CANFD1 带固定分裂终端：R20/R21 各 60.4 Ω，CANH1-CANL1 等效约 **120.8 Ω**，无法通过跳帽关闭。因此开发板应位于总线一端，另一端再启用一个 120 Ω 终端。断电测量时，只接本板约为 120 Ω；总线两端各有一个终端时约为 60 Ω。

## 最快演示：不需要第二个 CAN 节点

1. 编译、烧录并打开 SCI3 串口，参数为 115200-8-N-1。
2. 输入 `s` 开启离线合成流量。
3. 工程自动清空模型，先用正常流量学习 **5 秒**。
4. 随后依次注入 Unknown ID、DLC Change、Payload Jump 和 Flood；每种异常持续约 1.4 秒，之后循环。
5. 短按 S1 或输入 `u`，查看五页 UI；输入 `i` 查看诊断，输入 `e` 导出模型和事件。

`s` 模式不访问物理 CAN 总线，因此它能验证异常检测、状态机、日志和显示，但不能验证 MCP2542FD、接线或总线电气质量。

## 真实 CAN-FD 演示

需要一个真实的第二节点，推荐 USB-CAN-FD 适配器。适配器设置为：

- Nominal bitrate：500 kbit/s
- Data bitrate：2 Mbit/s
- CAN-FD：ISO
- BRS：发送 FD 帧时启用

上电或输入 `l` 后，保持总线仅有正常流量。真实总线模式只有同时满足以下两个条件才会自动结束学习：

- 已学习至少 10 秒；
- 已接收至少 100 帧。

如果帧数不足，界面会保持 LEARNING。学习结束后进入 MONITORING，再注入异常流量。不要在学习阶段注入攻击，否则异常会被当作正常基线。

真实总线的发送必须有其他节点提供 ACK。只有一块开发板时，不要在 Normal 模式反复使用发送命令，否则会出现 ACK Error，严重时进入 Bus-Off；可先输入 `o` 打开控制器内部回环，再用 `c` 或 `f` 验证发送 API，完成后再次输入 `o` 退出回环。

## 串口与按键

SCI3 使用 115200-8-N-1。USB 转串口模块交叉连接：模块 RXD → P409/TXD3，模块 TXD → P408/RXD3，GND → 开发板 GND，VCC 不接。

| 操作 | 功能 |
|---|---|
| `l` | 清空已有模型并重新学习正常流量 |
| `m` | 手动冻结当前模型并进入监测；尚无 CAN 帧时不会成功 |
| `s` | 开关完全离线的合成流量攻击演示，并重新开始学习 |
| `o` | 开关 CAN 控制器内部回环 |
| `c` | 发送一帧 Classical CAN 测试帧 |
| `f` | 发送一帧 CAN-FD+BRS 测试帧 |
| `u` | 切换到下一页显示界面 |
| `i` | 输出模型、帧率、负载、TEC/REC、丢帧和 Bus-Off 诊断 |
| `e` | 输出已学习的 ID Profiles 和最近 IDS Events |
| `q` | 输出一次供上位机解析的 `TELEM` 状态和最新 `FRAME` 快照 |
| `h` | 显示帮助 |
| S1 短按 | 切换到下一页显示界面 |
| S1 按住至少 0.8 秒 | 清空模型并重新学习 |

## 五页显示界面

1. **Dashboard**：LEARNING/MONITORING/BUS ERROR 状态、FPS、已学习 ID 数、TEC/REC、负载和当前异常分数。
2. **ID Profiles**：已学习 ID、DLC、报文数、平均周期和风险分数。
3. **Live Frames**：最近报文的 ID、长度、标志、数据预览和异常原因。
4. **Events**：最近异常的时间、ID、分数和原因。
5. **System Health**：最近 48 秒总线负载/风险双曲线、CAN/屏幕/接收队列/控制器健康灯、真实/离线/回环运行模式，以及 Anomaly、Unknown、Drop、Bus-Off、TEC/REC 累计计数。

S1 短按或 `u` 按顺序循环页面。异常或总线故障时，核心板蓝色 LED（P600，低电平点亮）用于告警。

## 编译与烧录

工程目标器件为 `R7FA8D1BHECBD`，当前配置使用 e² studio 2025-12、FSP 6.4.0 和 LLVM for ARM 21.1.1。

1. 在 e² studio 中选择 **File → Import → Existing Projects into Workspace**，导入本目录。
2. 打开 `configuration.xml`，确认活动 Pin Configuration 后点击 **Generate Project Content**。
3. 选择 `Debug` 配置并执行 **Build Project**。
4. 生成文件为 `Debug/ra8d1_canfd_edge_ids.elf`。
5. 使用工程自带的 `ra8d1_canfd_edge_ids Debug_Flat.launch`，通过板载调试器/J-Link 下载并运行。
6. 复位后串口应出现类似：

   ```text
   BOOT,RA8D1_CANFD_EDGE_IDS,DISPLAY,READY,CAN,READY,ERR,0,TIMEBASE,READY
   PROTO,NAME,RA8D1_CAN_IDS,VERSION,1,TRANSPORT,UART,BAUD,115200
   ```

`q` 为不改变设备状态的幂等快照命令，适合上位机以 2～5 Hz 轮询；消息字段与原因位定义见 [docs/HOST_PROTOCOL.md](docs/HOST_PROTOCOL.md)。

如果打开 `configuration.xml` 后重新生成代码，请不要手工修改 `ra/`、`ra_cfg/` 或 `ra_gen/` 下的生成文件；业务代码位于 `src/`。

## PC 端 python-can 流量脚本

安装依赖：

```powershell
python -m pip install -r tools/requirements.txt
```

Linux SocketCAN 的 CAN-FD 示例：

```bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on
sudo ip link set can0 up
python tools/canfd_demo_sender.py --interface socketcan --channel can0 --fd --attack cycle
```

脚本默认先发送 10 秒正常流量，再注入 2 秒异常，并循环 Unknown ID、DLC、Payload 和 Flood。只测试某一类异常时可将 `cycle` 换为 `unknown`、`dlc`、`payload` 或 `flood`。发送 Classical CAN 时省略 `--fd`。Windows 下根据适配器选择 python-can 后端，例如 PCAN 可使用 `--interface pcan --channel PCAN_USBBUS1 --fd`。

## 工程结构

- `src/can_ai/`：在线流量画像与可解释异常检测。
- `src/can_bus/`：RA8D1 CANFD Lite 驱动适配、ISR 接收队列、回环和测试发送。
- `src/app/`：时间基准、串口、合成流量和事件日志。
- `src/ui/`：五页 CAN IDS 显示状态机、精简中文点阵与轻量 RGB565 图形渲染。
- `src/display/`：SDRAM、ST7796U、MIPI DSI/GLCDC 显示适配；面板背光显式常亮，
  没有软件休眠超时。
- `tools/canfd_demo_sender.py`：PC 端可重复正常/攻击流量发生器。
- `docs/hardware/`：接线说明以及核心板、扩展板原理图副本。

## 限制

- 当前是可解释的在线统计异常检测，不是预训练神经网络；它判断“是否偏离本机学习到的正常流量”，不理解具体车辆或设备信号语义。
- 当前画像使用 ID/ID 模式、周期、DLC、Payload 和帧率；FDF/BRS/ESI 会显示在 UI，但尚未作为独立异常特征。同一 ID、长度和数据不变时仅切换帧格式可能不会告警。
- 模型保存在 RAM 中，复位或重新上电后需要重新学习；未实现 Flash 持久化或模型版本管理。
- 最多保存 32 个 ID 画像；ID 更多时会报告 Model Full，需按目标网络裁剪或扩容。
- 学习数据质量决定检测质量；正常工况覆盖不足可能误报，攻击混入学习阶段可能漏报。
- 固定 120.8 Ω 终端决定了本板必须位于总线端点，不适合直接挂在总线中间。
- 合成自测不经过 CAN 控制器和收发器，电气层、终端、ACK、线束和 EMC 必须用真实第二节点单独验收。
- 本工程是原型和作品集项目，不应直接作为汽车功能安全或生产安全系统使用。

## 快速验收清单

- [ ] J4 的 1-2、3-4、5-6 三组横向跳帽均已安装。
- [ ] 断电且未接另一端终端时，CANH1-CANL1 约为 120 Ω。
- [ ] 串口为 SCI3 115200-8-N-1，启动信息中的 DISPLAY、CAN、TIMEBASE 均为 READY。
- [ ] 输入 `s` 后先显示 LEARNING，约 5 秒后进入 MONITORING。
- [ ] 四类自测攻击都能在 Events 页面或串口 `EVENT` 行看到对应原因。
- [ ] `u`/S1 能循环五页，健康页趋势和状态灯更新，`i` 和 `e` 能输出完整诊断与模型信息。
- [ ] 接真实 USB-CAN-FD 后，500K/2M ISO 参数一致，断电测得整条总线约 60 Ω。
- [ ] 输入 `l` 后正常流量持续至少 10 秒且不少于 100 帧，再进入 MONITORING。
- [ ] 真实异常注入可被识别，且 `i` 中没有持续丢帧、ACK Error 或 Bus-Off。

## Windows 上位机

工程已包含统一的 **RA8D1 Edge AI Studio**，位置为 [`host_app/`](host_app/)。它通过 SCI3 115200-8-N-1 自动识别本 CAN IDS 固件或 `ra8d1_bmi088_vibration_ai` 固件，并切换 CAN 画像/事件或 BMI088 机器健康/波形/特征解释页面；端侧学习和推理仍由 RA8D1 完成。

快速体验：

Windows 可直接双击工程根目录的 `启动上位机.bat`，或运行
`host_app/dist/RA8D1_EDGE_AI_STUDIO/RA8D1_EDGE_AI_STUDIO.exe`，无需另装 Python。
仓库同时包含上位机源码、测试和这份完整 Windows 程序。

源码运行与无硬件演示：

```powershell
cd host_app
python -m pip install -r requirements.txt
python main.py --demo
python main.py --demo bmi
```

真实 P408/RXD3、P409/TXD3 接线、运行参数和 Windows EXE 打包方法见 [`host_app/README.md`](host_app/README.md)。

## AUTOSAR 风格 CAN/Bootloader 扩展

本工程还包含面向汽车嵌入式岗位的分层扩展：`Application → RTE-like → DCM/CanTp → CanIf-like → CANFD MCAL-like`，并预留 64 KiB Bootloader 区、UDS 诊断入口（`0x7E0/0x7E8`）。独立 `bootloader/` 已加入 SHA-256/ECDSA-P256、RA8D1 FSP Flash/RSIP 适配、连续块下载状态机和双 metadata 掉电提交；独立工程模板在 [`bootloader/project/`](bootloader/project/)。实现、分区约束和量产化路线见 [`docs/AUTOSAR_CAN_BOOTLOADER.md`](docs/AUTOSAR_CAN_BOOTLOADER.md)；第一阶段验证命令为：

```powershell
.\tools\run_autosar_tests.ps1
```

当前应用 ELF 仍保持原 CAN IDS 地址，Bootloader 分区是下一阶段独立工程和 linker 迁移的边界，不能把这一步误认为已通过车规量产认证。
