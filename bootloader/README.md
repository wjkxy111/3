# RA8D1 安全 CAN Bootloader 核心

本目录现在包含可独立编译/主机测试的 Bootloader 核心，以及 RA8D1 FSP 6.4 的
Flash/RSIP 适配边界。它与当前 `ra8d1_canfd_edge_ids` Application 工程分开，
不会因为应用工程重新构建而覆盖 Bootloader 区域。

## 已实现

- `autosar_secure_image.*`：256 字节（按 RA8D1 128-byte code-flash 写粒度对齐）版本化 manifest、SHA-256 payload 摘要、
  ECDSA-P256 raw `r || s` 签名域、向量/长度/安全计数器检查。
- `autosar_bootloader.*`：启动时选择跳转、升级或恢复；签名失败绝不跳转。
- `autosar_boot_update.*`：擦除 → 顺序接收 → 签名校验 → 双 metadata 提交状态机。
  块号/offset 单调、大小有上限、回滚计数器拒绝降级，掉电时只接受带 CRC 和最终
  commit marker 的最新 metadata。
- `autosar_boot_flash_fsp.*`：条件编译的 `r_flash_hp` 适配，包含 code-flash
  erase/write/blank-check、范围检查、写入粒度检查、critical hook 和 FSP
  anti-rollback counter 读/增/恢复。
- `autosar_boot_metadata_fsp.*`：将两条 64-byte metadata 记录落到 RA8D1 Data Flash；
  先写 body/CRC，最后单独写 4-byte commit marker，掉电时无效记录不会被选中。
- `autosar_boot_rsip.*`：条件编译的 RA8D1 RSIP-E51A SHA-256/ECDSA verify 适配。
  公钥必须是 FSP 生成并保护的 wrapped public key，私钥不在设备上。
- `project/`：独立 e² studio/FSP Bootloader 工程边界、Bootloader/Application
  linker 分区模板和机器可读分区清单。
- `tools/secure_image.py` + `tools/secure_sign_node.js`：主机生成密钥、打包和验签。
- `autosar_boot_uds_update.*`：独立 Bootloader 侧 UDS `0x34/0x36/0x37` 适配。
  `0x34` 是项目定义的 127-byte 安全下载请求，携带镜像版本、计数器、向量、摘要和
  签名；`0x36` 只接受连续 block counter，`0x37` 才触发最终验证/metadata 提交。

## 运行主机验证

```powershell
Set-Location C:\Users\34542\Desktop\端侧AI与车辆嵌入式项目\ra8d1_canfd_edge_ids
.\tools\run_autosar_tests.ps1
python .\tools\secure_image.py generate-key .\temp\prod_signing.pem
python .\tools\secure_image.py pack .\temp\signed_app.srec `
    .\temp\can.secimg --version 0x10000 --security-counter 1 .\temp\prod_signing.pem
python .\tools\secure_image.py verify .\temp\can.secimg .\temp\prod_signing.pem.pub.pem
```

`signed_app.srec` 必须来自使用 `bootloader/project/memory_regions_app_ext.lld` 的
Application 构建；当前演示 ELF 仍在 `0x02000000`，工具会故意拒绝它，避免把错误
reset vector 签成看似合法的镜像。

测试会覆盖 CRC/ISO-TP/DCM、SHA-256、合法/篡改/回滚/错误签名镜像、启动跳转和
擦除/写入/验签/metadata 提交状态机。主机工具生成的私钥只能留在离线签名环境，
不要提交到 Git 或烧录到 ECU。

## 量产工程必须完成的绑定

1. 按 `project/README.md` 建立独立 FSP 工程并加入 `r_flash_hp`、
   `r_rsip_e51a_protected`；不要把当前 Application 的 linker 当作 Bootloader linker。
2. 将 `AUTOSAR_BOOT_USE_FSP_FLASH`、`AUTOSAR_BOOT_USE_RSIP_E51A` 打开，绑定生成的
   `g_flash_ctrl/g_flash_cfg`、`g_rsip_ctrl/g_rsip_cfg` 和 wrapped public key。
3. 把 code-flash 擦写支持代码放入 RAM，并在擦除/写入期间关闭中断或使用 RAM 向量表；
   `r_flash_hp` 不能在 code flash P/E 期间取指或取中断向量。
4. 在 UDS 0x34/0x36/0x37 的诊断层调用 `autosar_update_begin/write/finish`，传输层
   必须保留顺序块号、超时、看门狗喂狗和掉电恢复。当前应用里的 DCM 仍只做诊断演示，
   不会伪报“下载成功”。
5. 量产前用 Renesas Flash Programmer 配置/锁定 anti-rollback counter、RSIP key
   provisioning、debug ID/安全区域，并做掉电、总线中断、重复包、错误签名、越界和
   看门狗压力测试。

## 与官方 MCUboot 的关系

本目录的 v2 manifest 是为了在现有工程中提供可审计、可主机测试的接口契约；它不是
对 MCUboot manifest 的替代。真正量产建议优先使用 Renesas FSP `rm_mcuboot_port`
生成的 Solution/Bootloader/Application 结构，并让官方 `imgtool.py` 生成镜像；若采用
本目录格式，必须冻结格式版本、密钥 ID、分区、计数器策略，并进行独立安全评审。
