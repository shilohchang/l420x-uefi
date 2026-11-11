# L420x / 麒麟9000C — V7 KDNET 可行性调查及离线配置工具

这是 **KDNET 早期调试接入前置工具包**，并不是已经能连上 WinDbg 的 USB KDNET 固件实现。它不会制造一个不存在的调试硬件，也不会仅靠注入 DBG2 就声称 Windows 能连接 WinDbg。

**现状：** bydjPE / Windows 10 ARM64 19045；注入实验 ACPI 后 Windows Boot Manager 能显示 `Loading files...`，结束后黑屏。现有 ACPI 会话注入 DSDT / FADT / MADT / GTDT，没有经过验证的 DBG2。kdnet.exe 需要在**目标 Windows** 识别控制器，但当前目标系统还无法引导，因此不能拿 Windows 11 x64 主机上的 `kdnet.exe` 检测 L420x。

## 0. 解压到原来的 main 工作区（无需分支）

Windows：将包放到 `C:\Users\icy\Downloads\L420X-V7-KDNET-Toolkit.zip`。

WSL Debian：

```bash
cd ~/l420x-uefi
unzip -n /mnt/c/Users/icy/Downloads/L420X-V7-KDNET-Toolkit.zip
# -n 不覆盖已有文件；没有创建分支或提交 Git
```

打包内容：

- `projects/diagnostics/KirinKdProbe/` — **只读 EFI**，检查 ACPI RSDP/XSDT/DBG2 及 SerialIo/USB2 HC 协议，不能证明 device-mode USB3 支持。
- `scripts/build-kdprobe-v7.sh` — 使用现有 Debian `GCC`（而非 GCC5）编译 AARCH64 EFI。
- `EFI/BOOT/V7-KD-PREFLIGHT.nsh` — Shell 中只执行只读探测，写入 U 盘文本文件，不加载 ACPI，也不启动 Windows。
- `scripts/inspect-kirin-dtb.py` — 扫描 L420x 自身 DTB 中的 USB/UART 线索。
- `scripts/inspect-dbg2.py` — 检查从设备导出的原始 DBG2 ACPI 表（如有）。
- `windows/Inspect-L420xBoot.ps1` — 在 Windows 11 主机上**只读**检查 E: 启动文件和 EFI PE 架构。
- `windows/Inspect-WinPE-KdTransport.ps1` — 可选：只读挂载 boot.wim，检测 ARM64 EEM 调试 DLL 是否存在（管理员权限，完成后 DISM /Discard）。
- `windows/Configure-L420xKDNET.ps1` — 默认只读。只有手动确认实际硬件后才允许对 **E: 的离线 BCD** 配置调试，并可还原。
- `windows/Start-L420xWinDbg.ps1` — 主机 WinDbg 启动器（需要实际可用的传输）。

## 1. 编译并只读探测 ACPI / DBG2

```bash
cd ~/l420x-uefi
bash scripts/build-kdprobe-v7.sh
# 结果：artifacts/v7/KirinKdProbe.efi

# 若 E: 在 WSL 未挂载，先确认 Windows E: 确实是测试 U 盘，再执行
sudo mkdir -p /mnt/e
sudo mount -t drvfs E: /mnt/e

mkdir -p /mnt/e/efi/boot/tools /mnt/e/efi/boot/logs
cp artifacts/v7/KirinKdProbe.efi /mnt/e/efi/boot/tools/
cp EFI/BOOT/V7-KD-PREFLIGHT.nsh /mnt/e/efi/boot/
sync
```

在 UEFI Shell，首先按**之前验证过的操作**安装实验 ACPI 表（测试风险自负，冷启动后会话生效）。安装成功后，**不要直接启动 Windows**，执行：

```text
fs8:\efi\boot\V7-KD-PREFLIGHT.nsh
```

读取 `E:\efi\boot\logs\v7-kdprobe.txt`，重点看 `ACPI20_RSDP`、`XSDT_Length`、`DBG2_NOT_PRESENT` 或 `DBG2_DEVICE` 和对应端口类型。没有 DBG2 时不能凭空填 `busparams=1`。就算存在 DBG2，也需确认 USB 控制器真实处于设备模式、控制器类型、寄存器、ACPI 命名空间设备与传输驱动匹配。

**限制：** 这份 EFI 是试验源码，当前工具制作环境没有你的 EDK2 AARCH64 构建链，未在 L420x 编译、启动或进行硬件测试。UEFI firmware 提供损坏的 XSDT 指针时，只读解析仍可能挂起；不要在不可恢复环境下运行。

## 2. DTB 确认 USB3 DRD / UART

只能使用从 **L420x 自己的 Linux/固件** 导出的设备树。`/sys/firmware/fdt` 是在目标设备 Linux 中导出的，不是 Debian WSL 的设备树。

若设备能进入 Linux，在设备上执行：

```bash
sudo cp /sys/firmware/fdt /tmp/l420x-live.dtb
# 将 /tmp/l420x-live.dtb 安全复制到 Debian 主机
```

在 WSL：

```bash
python3 scripts/inspect-kirin-dtb.py /path/to/l420x-live.dtb
python3 scripts/inspect-kirin-dtb.py /path/to/l420x-live.dtb --json
```

关注 `compatible`（`snps,dwc3` 等）、`dr_mode`（peripheral/otg/host）、USB-C 物理路由、`status` 和 `reg_untranslated`。DTB 显示 DWC3 不意味着 Windows KDNET EEM 可用；还必须确认 Synopsys USB3 双角色**实际设备模式**、KDNET 传输能力和 DBG2 配置。

有从真实固件导出的 DBG2 文件时：

```bash
python3 scripts/inspect-dbg2.py /path/to/DBG2.dat
```

## 3. Windows 11 主机：只读 BCD 检查

以普通或管理员 PowerShell 运行：

```powershell
Set-Location 'C:\Users\icy\Downloads\L420X-V7-KDNET-Toolkit\windows'
.\Inspect-L420xBoot.ps1 -Drive 'E:'
.\Configure-L420xKDNET.ps1 -Mode Audit -BcdStore 'E:\efi\microsoft\boot\BCD'
```

如果不能直接执行 PowerShell 脚本，可以使用：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Configure-L420xKDNET.ps1 -Mode Audit
```

以上操作不改写离线 BCD，也不改 Windows 主机的 BCD。

检查 boot.wim 内有没有 ARM 调试组件（可选，需要管理员 PowerShell、足够的主机磁盘空间）：

```powershell
.\Inspect-WinPE-KdTransport.ps1 -WimFile "E:\sources\boot.wim" -Index 1
```

该脚本将 WIM **只读挂载**到 Windows 临时目录并卸载/丢弃，默认不改 WIM。只有 `kd_8003_5143.dll` 存在不足以证明 KDNET 可用。

## 4. 只有在真实硬件条件满足后，才配置 KDNET EEM

**执行前必须全部完成：**

1. L420x USB-C 实际连接到可用的 Synopsys USB3 双角色控制器，支持 USB device mode；Windows 10 ARM64 19045 启动环境存在对应的 `kd_8003_5143.dll` 或其他适合目标的受支持 KDNET 传输（不能将主机 x64 传输 DLL 当成目标 ARM64 DLL）。
2. 发布了有效的 ACPI DBG2，端口信息、控制器寄存器地址、ACPI 命名空间节点均来自实际 L420x 硬件数据，而且你已**独立确认** USB EEM 对应 DBG2 条目编号。
3. Windows 主机安装 WinDbg / Debugging Tools，物理使用 USB 3.0 A↔C 数据线，按 Microsoft EEM 要求连接。确认这个调试 USB 控制器**不是加载 boot.wim 所依赖的唯一 USB 主机控制器**，否则切换 Device Mode 可能中断启动介质访问。
4. BCD 文件的备份路径可写、已了解离线 BCD 改动可能阻碍引导。

确认之后，在 **Windows 11 主机管理员 PowerShell** 中执行（下面的 `1` 只是示例，不代表 L420x 真实设备）：

```powershell
.\Configure-L420xKDNET.ps1 -Mode Enable `
  -BcdStore 'E:\efi\microsoft\boot\BCD' `
  -BusParams 1 -ConfirmedDbg2UsbEntry -Port 50005
```

脚本将按 Microsoft EEM 模式设置 `HOSTIP:169.254.255.255`、`NODHCP`、`busparams`、Boot Manager / Winload `bootdebug` 及 `{default}` `debug`；每次成功修改前会备份离线 BCD，配置失败则尝试恢复。随机生成的 KDNET 密钥只存放在 Windows 主机用户 Documents 下的受限文件，不会作为公开日志打印。

启动主机 WinDbg（仍然需要真实工作中的 USB EEM 传输）：

```powershell
.\Start-L420xWinDbg.ps1 -KeyFile 'C:\Users\icy\Documents\L420X-KDNET\kdnet-<timestamp>.key' -Port 50005
```

使用脚本打印的具体备份文件名回滚：

```powershell
.\Configure-L420xKDNET.ps1 -Mode Restore `
  -BcdStore 'E:\efi\microsoft\boot\BCD' `
  -BackupFile 'E:\efi\microsoft\boot\BCD.v7-kdnet-<timestamp>.bak'
```

**切勿将包含调试密钥的 BCD、截图或 `.key` 文件上传公开 GitHub 仓库。**

## 5. 结果判定

| 观察结果 | 技术结论 |
|---|---|
| `DBG2_NOT_PRESENT` | 当前注入 ACPI 未提供调试端口表；还需硬件确认，不启用 KDNET |
| DBG2 包含 USB 类型但不是可用 DWC3 Device Mode | USB EEM 仍未就绪 |
| DTB 列出 `snps,dwc3` + `dr_mode=peripheral/otg` | 只是候选；需验证 Port/Type-C 路由、供电、PHY、固件初始化和 Windows 传输 |
| WinDbg 无连接，设备 `Loading files...` 后黑屏 | 无法区分未初始化调试传输还是系统死锁；不能据此确定 Winload 已运行 |
| WinDbg 在 Boot Manager 连接，随后断开 | 缩小到 OS Loader 交接 / transport 切换 / ACPI 与 USB 控制器状态 |
| WinDbg 在 Winload 或内核阶段连接 | 才能开始通过堆栈、异常和符号判断具体卡点 |

## Microsoft 参考（2026-11 核查）

- [ARM USB KDNET EEM](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-kernel-mode-debugging-over-usb-eem-arm-kdnet)
- [ACPI DBG2 表结构](https://learn.microsoft.com/en-us/windows-hardware/drivers/bringup/acpi-debug-port-table)
- [离线 UEFI BCD 的 KDNET 配置](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/validation-os-debug-apps)
- [Boot Manager/Winload/Kernel 调试选择](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/setting-up-a-network-debugging-connection-automatically)

## 安全

- 无固件刷写、无 NVRAM 变更、无 GPIO/UART/USB MMIO 读写。
- KDNET 的 Enable 只有用户显式批准已经验证的 DBG2/USB 调试硬件时才允许；默认 Audit 是只读。
- 不创建 Git 分支、不自动提交或推送。
