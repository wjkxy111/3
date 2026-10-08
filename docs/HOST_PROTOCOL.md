# RA8D1 CAN IDS 上位机串口协议

该协议用于上位机读取 RA8D1 上的检测状态和最新 CAN/CAN-FD 帧。传输层为 SCI3 UART，固定 `115200-8-N-1`。所有消息均为 ASCII，一行一条，以 `\r\n` 结束，字段以英文逗号分隔。

## 协议发现

固件复位后保留原有 `BOOT` 行，并紧接着输出协议声明：

```text
PROTO,NAME,RA8D1_CAN_IDS,VERSION,1,TRANSPORT,UART,BAUD,115200
```

上位机应按 `NAME` 和 `VERSION` 选择解析器。未知的消息类型或字段应忽略，以便兼容后续版本。

## 快照命令

发送单字符 `q` 请求一次快照。`q` 是幂等命令，不会开关后台推流，也不会改变检测器状态。每次请求必定输出一条 `TELEM`；自上次成功快照后收到更新帧时，再输出一条 `FRAME`。总线停止时不会重复发送相同 Payload。

建议轮询频率为 2～5 Hz。满载 64 字节 CAN-FD 帧时，一次响应约 480 个 ASCII 字节；不要无间隔连续发送 `q`。

### TELEM

```text
TELEM,T_MS,123456,MODE,MONITOR,DEMO,OFF,LOOPBACK,OFF,FPS,824,LOAD_PM,217,IDS,12,SCORE,700,REASON,0x00000004,TOTAL,90817,ANOMALY,31,UNKNOWN,2,HEALTH,OK,TEC,0,REC,0,DROP,0,BUSOFF,0
```

| 字段 | 含义 |
|---|---|
| `T_MS` | 设备启动后的毫秒数，超过 `UINT32_MAX` 时饱和 |
| `MODE` | `LEARN` 或 `MONITOR` |
| `DEMO` | 离线合成攻击演示是否启用 |
| `LOOPBACK` | CAN 控制器内部回环是否启用 |
| `FPS` | 最近统计窗口的总线帧率 |
| `LOAD_PM` | 估算总线负载，千分数 `0..1000` |
| `IDS` | 已学习的 CAN ID 画像数 |
| `SCORE` | 当前告警分数 `0..1000` |
| `REASON` | 当前可解释告警原因位掩码 |
| `TOTAL` | AI 检测器累计处理帧数，超过 `UINT32_MAX` 时饱和 |
| `ANOMALY` | 累计异常帧数，超过 `UINT32_MAX` 时饱和 |
| `UNKNOWN` | 累计未知 ID 帧数 |
| `HEALTH` | `OK`、`NA`、`BUS_ERR` 或 `DISP_ERR` |
| `TEC` / `REC` | CAN 发送/接收错误计数器；状态不可用时为 0 |
| `DROP` | 软件队列丢帧、硬件 message lost 与非法帧的累计和 |
| `BUSOFF` | 累计进入 Bus-Off 的次数 |

`REASON` 使用 UI/上位机统一的位定义：

| 位 | 值 | 原因 |
|---:|---:|---|
| 0 | `0x01` | Unknown ID |
| 1 | `0x02` | Timing |
| 2 | `0x04` | Flood |
| 3 | `0x08` | DLC Change |
| 4 | `0x10` | Payload Jump |
| 5 | `0x20` | CAN Bus |
| 6 | `0x40` | Model Full |

### FRAME

```text
FRAME,SEQ,90817,T_MS,123455,ID,0x00000123,FMT,STD,FD,ON,BRS,ON,RTR,OFF,DLC,12,SCORE,0,REASON,0x00000000,DATA,00112233445566778899AABB
```

| 字段 | 含义 |
|---|---|
| `SEQ` | 每处理一帧递增的快照序号；用来区分同一毫秒内的多帧和重复轮询 |
| `T_MS` | 该帧的设备时间戳（毫秒），超过 `UINT32_MAX` 时饱和 |
| `ID` | 8 位十六进制 CAN ID；有效范围由 `FMT` 决定 |
| `FMT` | `STD` 或 `EXT` |
| `FD` / `BRS` / `RTR` | 帧格式标志，值为 `ON` 或 `OFF` |
| `DLC` | 本协议中表示解码后的数据字节数 `0..64`，不是原始 4 位 DLC 码 |
| `SCORE` / `REASON` | 此帧检测时的异常分数和原因位 |
| `DATA` | 连续大写十六进制，无空格；按 `DLC` 输出全部 `0..64` 字节 |

即使屏幕的 Live Frames 页面只显示前 8 字节，`FRAME` 仍保留并输出完整 CAN-FD Payload。远程帧或零长度帧的 `DATA` 值为空，行仍以 `DATA,` 结束。`q` 重复返回同一个最新帧时 `SEQ` 不变，上位机不应把它重复计数。

## 兼容的原有消息

版本 1 没有删除或改变既有 `BOOT`、`EVENT`、`MODEL`、`DIAG`、`AI`、`CAN`、`PROFILE`、`LOG`、`SELF_TEST`、`LOOPBACK` 和 `TX` 消息。上位机可继续用 `i` 获取详细控制器状态和接收计数，用 `e` 获取画像及最近事件。
