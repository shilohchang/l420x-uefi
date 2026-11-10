# GOP V2 read-only diagnostic

- This application enumerates GOP handles, current modes, queryable modes, pixel masks, device paths and the memory descriptor containing FrameBufferBase.
- No `SetMode`, `Blt`, raw framebuffer access, or MMIO writes are performed.
- Windows candidate check is **field-only**; it does not prove physical framebuffer accessibility, native panel timing, DPU persistence after ExitBootServices, or successful Windows boot.
- `ConsoleOutHandle` equality with a GOP handle is not a definitive console-to-GOP association.
- Copy the package contents into `~/l420x-uefi/`, then run `bash scripts/build-gopdiag.sh` within the existing EDK2 build environment.
- Run the resulting EFI from UEFI Shell. If shell redirection is supported: `KirinGopDiag-V2-AARCH64.efi > fsX:\\gop-v2.txt` (replace X with a writable FS mapping).
