# L420x KirinGopCompat V2 (experimental)

## Purpose

Adapts a **previously active** 2880x1920 GOP from reported RGB8 to BGRA8,
**only after** red, green and blue pixel checks confirm a B,G,R,X physical
framebuffer byte layout. No DPU format registers are written. This is a GOP
metadata adapter and **not** RGB-to-BGRA conversion.

The test is based on earlier KirinPixelProbe logs, where red (255,0,0) produced
`00 00 FF 00`. That is encouraging but not enough to prove Windows boot
compatibility; V2 expands the checks to three primaries.

## Key design decisions

- Only the GOP handle exposing `DevicePath` and matching 2880x1920 and RGB8
  is considered. Other virtual/duplicate GOP handles are left untouched.
- Exposes just one mode, mode 0, which represents the **currently active**
  hardware scanout mode. `SetMode(0)` succeeds *only if* the underlying
  GOP has not been modified. No hardware mode switch is attempted.
- `QueryMode(0)` allocates a fresh structure for the caller.
- `Blt()` forwards to the original physical GOP after checking for relocation.
- `Unload()` attempts to reinstall the original GOP protocol pointer.
- Framebuffer bytes are accessed by this test: running on an unexpected
  firmware memory mapping may fault or hang. Restoration is best effort.

## Install on WSL

Unzip the archive and merge its `projects/`, `scripts/`, and `docs/` directories
at the root of `~/l420x-uefi` (do not put the zip contents inside `edk2/`).

```sh
cd ~/l420x-uefi
bash scripts/build-gopcompat-v2.sh
```

Output: `artifacts/gop/KirinGopCompatV2.efi` (not built or verified by the author
in a matching cross-build environment).

## Test from a disposable UEFI Shell boot

Keep an unmodified boot option/USB available. Do not add to `startup.nsh`.

```text
fs8:
load \EFI\BOOT\tools\KirinGopCompatV2.efi
\EFI\BOOT\tools\KirinGopDiag.efi > fs8:\gop-v2-after.txt
```

`fs8:` is an example: confirm mapping via `map -r`. The `load` command prints
an image handle; use `drivers`/`dh` and Shell's `unload` with the *actual image
handle* to revert (Shell commands/handle identities vary). Prefer rebooting
between compatibility-driver and Windows-boot experiments.

## Remaining limitations

1. **Not Windows validated.** No ExitBootServices or Windows Boot Manager test.
2. A GOP consumer may retain the original protocol pointer or the duplicate
   GOP without DevicePath, so this does not guarantee global BGRA visibility.
3. No support for actual hardware mode switching or newly allocated framebuffer.
4. `SetMode(0)` does not clear the screen or trigger reinitialization.
5. Only tests byte order of a single pixel at three color values, and does not
   prove the panel's scanout interpretation.
6. Driver unload requires no active consumer holding the proxy pointer.
7. Boot-time framebuffer memory mapping and cache coherency have not been tested.
8. UEFI Secure Boot may reject unsigned test binaries.

If Windows still hangs, capture the last visible stage, BCD/boot-manager error,
ACPI table logs, GOP state before/after load, and screen behavior. Do not
conclude GOP is the sole boot blocker.
