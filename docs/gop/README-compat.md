# L420x GOP format compatibility — experimental

## Observed
Two GOP handles, RGB8 (`PixelFormat=0`), 2880x1920 stride 2880, one 0x0 mode. In a second capture the handles advertised overlapping framebuffer ranges. Do not assume the second GOP is independent hardware.

## Two important cases

1. **Firmware reports RGB8 but backing bytes are B,G,R,X:** a guarded GOP metadata compatibility layer can legitimately expose BGRA8. The DXE driver in this package probes one red pixel via GOP BLT, reads its raw bytes, restores the pixel, and only then replaces the physical GOP protocol on a handle that has DevicePath.
2. **Backing bytes are actually R,G,B,X:** this driver *refuses* to patch. A true Windows-compatible fix requires configuring the DPU scanout (e.g. channel swap / output pixel format) or a guaranteed hardware-backed BGRA scanout buffer. A Boot Services timer or BLT-only software conversion cannot follow direct Windows writes after ExitBootServices. No DPU register writes are included.

## Usage

Run `bash scripts/build-gopcompat.sh`, then copy the EFI executables to the test USB stick. In UEFI Shell run `KirinPixelProbe.efi` first. This **temporarily paints and restores pixel (0,0)**. Capture the entire output. If it says BGRA memory order, you may experiment with `load KirinGopCompatDxe.efi` and then run `KirinGopDiag.efi`. The driver changes the live GOP interface and is not read-only, so use only on a recoverable test boot and do **not** add it to `startup.nsh` or the permanent boot flow yet. Reboot to restore firmware state.

The driver exposes exactly one native mode because mode 2 is invalid and switching modes can change FrameBufferBase. The `SetMode(0)` wrapper is a no-op, so it is **experimental**, not proof of complete UEFI GOP compliance. It does not fix ACPI/BCD, memory attributes, display scanout after ExitBootServices, or duplicate GOP association.

**Safety limitations:** direct framebuffer reads require the advertised address to be CPU-accessible. The memory map check in previous logs identifies a reserved range but is insufficient proof of accessibility. On hardware where BLT targets a separate buffer, the probe may be inconclusive. Save work and use removable media; hang or display corruption is possible. Never run this as a permanent boot driver before a verified recovery method is available.
