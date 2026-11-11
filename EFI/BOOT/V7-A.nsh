@echo -off
# ACPI + pinned GOP (variant A) + Windows PE. Run from \EFI\BOOT on the USB volume after a COLD boot.
# Use only the variant whose green-strip self test was visible.
if not exist tools\KirinGopPinA.efi then
  echo ERROR: tools\KirinGopPinA.efi missing (cd to \EFI\BOOT first)
  goto END
endif
load tools\AcpiTableDxe.efi
tools\KirinAcpiSessionLoaderAuto.efi
if not %lasterror% == 0 then
  echo ERROR: ACPI install failed
  goto END
endif
load tools\KirinGopPinA.efi
tools\KirinBootV6.efi
:END
