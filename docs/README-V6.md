# KirinBootV6 / L420x diagnostic chainloader

**Goal:** separate EFI LoadImage failure from StartImage handoff hang, while capturing dual GOP and firmware memory descriptors. This does not itself repair the underlying black screen or guarantee Windows Logo/BSOD.

Install package content at `~/l420x-uefi` on WSL Debian (same main branch; no branch creation); run `bash scripts/build-boot-v6.sh`. Copy `artifacts/v6/KirinBootV6.efi` to `E:\EFI\BOOT\tools\` and both `V6-*.nsh` to `E:\EFI\BOOT\`. Do not overwrite `pe.efi`.

After cold boot use `fs8:\EFI\BOOT\V6-CLEAN.nsh`, provided your USB is still fs8. Read `E:\EFI\BOOT\logs\V6-TRACE.TXT` after restart. Log appends; the latest START_NEW_RUN denotes the latest run. Driver does NOT change GOP, framebuffer, ACPI, interrupt controller, CPU mode, or firmware variables. The optional V6-ACPI.nsh does mutate temporary ACPI tables via your existing experimental installer: use only for a controlled comparison, not as proof of bootable ACPI. Cold power-off between profiles.

A last trace line BEFORE_StartImage indicates the wrapper called StartImage; it cannot prove the Windows kernel reached execution. The wrapper cannot log after Boot Services end and does not intercept ExitBootServices. Runtime-region attributes and the *known* 0x51DD0000 framebuffer region are logged for analysis, not modified. Display loss could still be entirely independent of GOP.

Build source is a minimal evolution of V5 chainloader. It has not been built or device-tested by the assistant: report any compiler errors. To revert simply stop calling V6 EFI, remove it from the USB if desired, and cold boot. No NVRAM boot option changes.
