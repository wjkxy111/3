# 可借鉴的开源车辆工程组件

本项目不直接复制 GPL/商业 AUTOSAR 代码，而是保留接口契约并在 RA8D1 FSP 上做
独立适配。以下仓库适合作为架构和测试参考：

## 安全启动/升级

- [mcu-tools/mcuboot](https://github.com/mcu-tools/mcuboot)：Apache-2.0，提供跨 MCU
  的安全启动、双 slot/交换策略和 `imgtool` 签名工具。RA8D1 的量产版本优先使用
  Renesas FSP `rm_mcuboot_port`，本仓库的 v2 manifest 只作为现有 CAN 工程的可测
  过渡契约。

## UDS/ISO-TP 验证器

- [pylessard/python-udsoncan](https://github.com/pylessard/python-udsoncan)：MIT，
  可在 PC 侧验证 DiagnosticSession、RequestDownload、TransferData、ECUReset 和
  NRC 行为；与 `python-can`/`can-isotp` 配合使用。
- [hardbyte/python-can](https://github.com/hardbyte/python-can)：LGPL-3.0，提供跨
  Windows/Linux 的 CAN/CAN-FD 适配和日志回放。当前上位机仍可通过 UART 观察 IDS；
  有 USB-CAN-FD 后再启用直接总线抓包。

## 本项目的落地边界

开源仓库只用于协议、状态机、测试和工具链对照；设备端关键路径仍由
`autosar_secure_image`、`autosar_boot_update`、FSP Flash 和 RSIP 适配组成。这样可以
在作品集中说明许可证、密钥生命周期和硬件绑定，而不是把通用库冒充车规认证栈。
