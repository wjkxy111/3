# RA8D1 Secure Bootloader 独立工程边界

这里是量产 Bootloader 的独立工程说明和 linker 分区模板。当前 CAN IDS 工程仍是
Application，不把 Bootloader 源码自动加入它的 Debug 构建，避免一次下载覆盖
Bootloader 区域。

## 在 e² studio/FSP 6.4 建立工程

1. 新建 RA8D1、Cortex-M85、LLVM 21.1.1、无 RTOS 的工程，工程名建议
   `ra8d1_secure_bootloader`。
2. 在 Stacks 中加入 `r_flash_hp`、`r_rsip_e51a_protected`、SCI/CAN-FD 所需
   驱动；若采用 Renesas 官方 MCUboot 集成，则按 `rm_mcuboot_port` 的
   Solution/Bootloader/Application 结构生成，不要把本目录的自定义头直接当作
   MCUboot manifest。
3. 将 `src/` 和 `include/` 加入这个 Bootloader 工程，并定义
   `AUTOSAR_BOOT_USE_FSP_FLASH`、`AUTOSAR_BOOT_USE_RSIP_E51A`。生成的
   `g_flash_ctrl/g_flash_cfg`、`g_rsip_ctrl/g_rsip_cfg` 和 RSIP wrapped public key
   由工程配置提供；私钥绝不进入固件。
   RA8D1 code-flash 写粒度是 128 bytes；把 `autosar_update_port_t.write_granularity`
   设为 128，更新状态机会把 CAN-TP 的非对齐块先在 RAM 中聚合再写入。
   将 `advance_counter` 绑定到 `autosar_boot_flash_fsp_advance_counter`，并把
   `anti_rollback_counter` 设为已在 RFP 配置/锁定的安全计数器。
4. 以 `memory_regions_bootloader.lld` 为 Bootloader 的 Flash 区域，以
   `memory_regions_app_ext.lld` 为 Application 的 Flash 区域。FSP 生成的
   `fsp.lld` 应 `INCLUDE` 其中一个区域文件；不要手改 `ra_gen/`。
5. 在代码 Flash 擦写期间，所有支持代码必须位于 RAM，且关闭中断或把向量表/ISR
   全部迁移到 RAM。`autosar_boot_flash_fsp.c` 的 critical hook 由平台层实现。

## 分区契约

| 区域 | 起始 | 大小 | 用途 |
|---|---:|---:|---|
| Bootloader | `0x02000000` | `0x00010000` | 不可被 OTA 擦除 |
| Secure slot | `0x02010000` | `0x001E8000` | header + Application |
| App vector/payload | `0x02010100` | `0x001E7F00` | 签名镜像执行区 |
| Data Flash metadata | `0x27000000` | `0x00000400` | 两条掉电安全提交记录 |

`memory_regions_app_ext.lld` 只是模板，最终必须用 map 文件确认 section 没有越界，
并确认启动代码把 VTOR 设置为 `0x02010100`。第一次量产烧录应同时写入 Bootloader、
签名 Application、RSIP 公钥/wrapped key 和 anti-rollback 初始配置。

## 启动流程

```text
Reset → 读取双 metadata → 检查 update request
      → 读 header → SHA-256 payload → RSIP ECDSA-P256 验签
      → anti-rollback counter 检查 → 设置 VTOR → 跳到 0x02010100
      └─失败/新升级 → UDS 0x34/0x36/0x37 接收 staging slot → 校验后提交 metadata
```

独立工程的真实下载/调试配置应使用 Bootloader 专用 `Debug_Flat`，并关闭调试器
的 flash 内容缓存。当前仓库只提供核心、FSP 适配器和可验证模板；没有伪造一个
未经过 FSP 生成/硬件验证的 `.elf`。
