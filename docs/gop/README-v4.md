# KirinGopCompat V4 (experimental)

Use only one of the following per fresh UEFI Shell boot:

- **Meta**: patch physical GOP pixel format and query for native resolutions; leave modes and firmware SetMode behavior intact.
- **Native**: physical GOP only; expose one 2880x1920 BGRA8 mode (0), no underlying SetMode call. Accept original mode 0 or mode 3.
- **Dual**: as Native, but also patch second GOP if it shares exactly the same framebuffer base and has native geometry; virtual GOP is optional.

All three are firmware experiments, not guaranteed UEFI compliant. They do NOT change DPU registers. Each profile directly modifies live protocol memory and is not compatible with stacking. Do not auto-load. Reboot between tests. Test on hardware with physical recovery access.

Suggested sequence (device containing EFI and Windows boot loader mapped appropriately):

```text
load fs8:\EFI\BOOT\tools\KirinGopV4Meta.efi > fs8:\v4-meta-load.txt
fs8:\EFI\BOOT\tools\KirinGopDiag.efi > fs8:\v4-meta-gop.txt
```

Only if GOP diagnostic and Shell work, execute existing Windows Boot Manager by its VERIFIED absolute path. Never mix multiple profiles in one boot. If unstable, reboot and choose next profile.

Expected errors include GOP interface misuse, console freeze, unrecoverable black screen until reboot, firmware memory fault or nonfunctional boot manager. Avoid modifying persistent firmware variables.

Source only; compilation in the target EDK2 tree and runtime hardware validation are required.
