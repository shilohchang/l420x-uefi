# V5.1 Safe GOP test (Kirin 9000C)

This is **not** a proven Windows black-screen fix. It removes V4's most aggressive mode-number spoofing while keeping a narrowly scoped, previously pixel-probe-supported BGRA metadata correction. It DOES NOT fix ACPI, framebuffer runtime memory attributes, DPU continuity, BCD, or any hardware BSOD on its own.

Changes vs V4:

1. Only **one** qualifying GOP associated with a DevicePath may be modified; otherwise refuse.
2. Preserve original `Mode`, `MaxMode`, `FrameBufferBase`, `FrameBufferSize`.
3. `SetMode()` calls the original firmware routine; no fabricated success.
4. `QueryMode()` changes RGB to BGR **only on the current physically identified mode**, leaving inactive modes unchanged.
5. No replacement Handle, no `ReinstallProtocolInterface()`, no MMIO or NVRAM operations.
6. Explicitly refuse to infer a framebuffer format for modes not confirmed. After unloading, cold reboot; mode metadata is not rolled back because firmware may have replaced Info.

## Windows WSL Debian

Extract ZIP into `~/l420x-uefi` while staying on `main`:

```bash
cd ~/l420x-uefi
unzip -o /mnt/c/Users/icy/Downloads/KirinGopV5_1.zip
bash scripts/build-gop-v5.1.sh
```

Result: `artifacts/gop/v5.1/KirinGopV51Safe.efi`. Copy to USB `E:\EFI\BOOT\tools\` using Explorer or PowerShell.

## Shell: manual test, one cold boot only

Verify USB mapping (`map -r`) and files. The example below assumes your verified mapped volume is `fs8:`. If different, change the prefix. Do not automatically install the driver.

```
fs8:
cd \EFI\BOOT
if not exist fs8:\EFI\BOOT\logs then
  mkdir fs8:\EFI\BOOT\logs
endif
fs8:\EFI\BOOT\tools\KirinGopDiag.efi > fs8:\EFI\BOOT\logs\v51-before.txt
load fs8:\EFI\BOOT\tools\KirinGopV51Safe.efi > fs8:\EFI\BOOT\logs\v51-load.txt
fs8:\EFI\BOOT\tools\KirinGopDiag.efi > fs8:\EFI\BOOT\logs\v51-after.txt
type fs8:\EFI\BOOT\logs\v51-load.txt
```

**Before loading `pe.efi`, perform the exact ACPI initialization that worked in V4 and verify BCD/media.** If GOP diagnostics changed unexpectedly, STOP and cold reboot. Manually launch the already-verified Windows Boot Manager path; do not assume `fs8:` is the Windows ESP. Do not combine with V4 patches in the same boot.

If hangs persist, save `v51-before.txt`, `v51-load.txt`, `v51-after.txt`, existing ACPI logs and a photo of the last screen. A Shell log cannot prove that Windows advanced past `StartImage()`.
