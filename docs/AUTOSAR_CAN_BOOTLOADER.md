# RA8D1 AUTOSAR 风格 CAN ECU + Bootloader 设计说明

## 定位

这是一个面向汽车嵌入式岗位作品集的 AUTOSAR 风格实现：接口和分层参考 Classic Platform 的 Application / RTE / BSW 思路，但没有复制商业 AUTOSAR 供应商栈，也没有声称通过 AUTOSAR 认证。

```text
CAN IDS / 诊断应用
        │
RTE-like autosar_stack（组合与调度）
        │
DCM（UDS）── CanTp（ISO-TP）
        │
CanIf-like adapter（0x7E0 / 0x7E8）
        │
现有 can_bus_port（FSP CANFD1 / MCAL-like）
```

## 已落地功能

- CAN Classic ISO-TP 单帧和多帧收发，接收超时 1 s，最大 128 字节。
- DCM 服务：`0x10` 会话控制、`0x11 01` ECU Reset 应答、`0x19 02` 无 DTC 查询、`0x22 F190/F186` 数据读取、`0x31 01 02 01` 镜像校验例程。
- Boot image header：magic、版本、长度、CRC32、初始栈、复位向量；拒绝越界、非 Thumb 复位地址和错误 SRAM 向量。
- Bootloader 选择器：检测更新请求，校验应用 slot，决定跳转、进入升级或进入恢复态。
- 主循环中已接入 `autosar_stack_on_can_frame()`，不会改变 CAN IDS 原有分析和屏幕逻辑。

## 安全升级核心（v2）

`bootloader/` 现在提供独立的安全核心，不会自动加入当前 Application 的 linker：

- `autosar_secure_image.*` 固定 256 字节（按 RA8D1 128-byte code-flash 写粒度对齐）manifest，payload 用 SHA-256，签名域用
  ECDSA-P256 raw `r||s`，启动前检查长度、SRAM 初始栈、Thumb reset、key id 和
  security counter。
- `autosar_boot_update.*` 实现受限的擦除/接收/验证/提交状态机。写入 offset 必须
  连续，不能越过 staging slot；验证成功后才向双 metadata journal 写入新序列号。
- `autosar_boot_flash_fsp.*` 和 `autosar_boot_rsip.*` 是 RA8D1 FSP 适配层，只有在
  独立工程定义宏并绑定生成的 FSP handles 后才启用硬件 API。
- `tools/secure_image.py` 可离线打包/验签；私钥不进入 Bootloader、Application 或
  上位机。

这比“CRC + 跳转”更接近量产链路，但自定义 manifest 仍不是 MCUboot 的兼容格式。
量产版本应优先采用 FSP `rm_mcuboot_port` 的官方 Solution 结构和 `imgtool.py`，并
由安全评审确认密钥 provisioning、anti-rollback、掉电恢复和 debug 生命周期。

独立 Bootloader 侧的 `autosar_boot_uds_update.*` 已把 UDS `0x34/0x36/0x37` 映射到
安全更新状态机：`0x34` 携带固定 slot 的安全元数据，`0x36` 按连续块号写 payload，
`0x37` 只在完整 payload、摘要和 RSIP 签名都通过后返回肯定响应。当前 Application
中的 DCM 仍不启用下载服务，避免把演示固件误当量产 Bootloader。

## UDS 试验帧

请求为标准 CAN ID `0x7E0`，响应为 `0x7E8`。单帧请求的第一个字节是 ISO-TP 长度，例如：

```text
02 10 03 00 00 00 00 00  →  06 50 03 00 32 01 F4 00
03 22 F1 86 00 00 00 00  →  04 62 F1 86 <session>
04 31 01 02 01 00 00 00  →  05 71 01 02 01 <status>
```

`0x31` 的 status 在当前应用里默认是 `READER`，因为还没有把 Renesas Flash API 和真实 Bootloader 工程绑定；这是一项刻意的安全行为，不会伪报“校验成功”。

## Flash 规划

```text
0x02000000 ─┬─ 64 KiB Bootloader
0x02010000 ─┬─ 256 B secure image header
0x02010100 ─┴─ application vector table + signed image payload
```

当前 CAN 演示 ELF 仍链接在 `0x02000000`，因此不能直接作为分区后应用烧录。独立
工程 linker 模板在 `bootloader/project/`。真正量产化还必须完成 FSP 工程生成、
应用 linker 偏移、Flash 擦写、硬件 RSIP key provisioning、UDS 0x34/0x36/0x37、
回滚/掉电恢复、看门狗和硬件压力测试。

## 构建与验证

```powershell
# RA8D1 固件
Set-Location C:\Users\34542\Desktop\端侧AI与车辆嵌入式项目\ra8d1_canfd_edge_ids
& C:\ti\ccs2031\ccs\utils\bin\gmake.exe -C Debug -j4 all

# 主机侧 CRC / ISO-TP / DCM / secure update 测试
.\tools\run_autosar_tests.ps1
```

当前实现已通过 RA8D1 LLVM 工程构建和严格 GCC 主机测试；刷写前应保留备份，不能把“骨架通过编译”理解为已完成车规 Bootloader。

> 说明：新 C 文件已加入当前 `Debug/src/app/subdir.mk`。如果在 e² studio 中重新生成 FSP/工程 Makefile，请确认这些 `autosar_*.c` 仍出现在 app 源文件清单中；不要把业务文件放进 `ra/`、`ra_gen/` 或 `ra_cfg/`。
