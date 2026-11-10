# KirinGopCompat V3 – L420x experimental display handoff

## Motivation

V2's test-mode logging completed, but the earlier actual installation froze near
`ReinstallProtocolInterface()`. V3 deliberately avoids protocol reinstall and
firmware reconnect notifications by patching the existing physical GOP
protocol in place. This remains a potentially hazardous firmware experiment.

Confirmed on this test device (before V3): three primary-color BLT probes
returned raw framebuffer B,G,R,X bytes while GOP reported RGB8.

## Implementation

- Requires physical GOP with DevicePath, active Mode 0, 2880x1920,
  stride 2880, RGB8 metadata and enough framebuffer memory.
- Changes the active mode info pixel format to `PixelBlueGreenRedReserved8BitPerColor`.
- Limits advertised modes to mode 0, and substitutes a `QueryMode()` wrapper.
- `SetMode(0)` is a guarded no-op: **not** a complete GOP mode-setting implementation.
- Keeps original `Blt()` unmodified.
- Does **not** write the framebuffer, touch DPU registers, relocate memory,
  create a second GOP, or call `ReinstallProtocolInterface()`.
- Unload attempts to restore the original pointers and GOP metadata.

## Installation / test

In UEFI Shell on a fully recoverable test boot, with no other GOP adapter loaded:

```
fs8:
cd \EFI\BOOT\tools
load KirinGopCompatV3.efi
KirinGopDiag.efi > fs8:\gop-v3-after.txt
```

Check physical GOP and its `QueryMode(0)` and framebuffer. A virtual GOP may
continue to report RGB8; the two GOPs should not be assumed synchronized.

Only after GOP and console stay stable should you attempt to invoke the
existing, known Windows Boot Manager EFI path, after verifying the Windows
ESP and BCD. Do not blindly point `fs8:` at the Windows boot manager: in the
previous test it was a USB diagnostics volume, not the internal Windows ESP.

## Important limitations

- A successful GOP audit is not proof that Windows will boot. ACPI, timer,
  CPU/EL level, boot storage, BCD and ExitBootServices remain separate risks.
- Direct in-place patching is intended only for experimentation; no automatic
  startup.nsh installation, no firmware flash, no permanent changes.
- Driver `load` may itself trigger Shell graphics activity. On a hang, restart
  the machine; the patch is in volatile memory only.
- UEFI drivers that cached the original QueryMode pointer will bypass the
  wrapper. The firmware may overwrite Info fields during other operations.
- Native `SetMode(0)` is not re-executed. If Windows requires a real
  reinitialization, this workaround may still fail.
