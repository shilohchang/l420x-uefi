@echo -off
# ACPI + pinned GOP (variant B) + Windows PE. Run from \EFI\BOOT on the USB volume after a COLD boot.
# Use only the variant whose green-strip self test was visible.
if not exist tools\KirinGopPinB.efi then
  echo ERROR: tools\KirinGopPinB.efi missing (cd to \EFI\BOOT first)
  goto END
endif
load tools\AcpiTableDxe.efi
tools\KirinAcpiSessionLoaderAuto.efi
if not %lasterror% == 0 then
  echo ERROR: ACPI install failed
  goto END
endif
load tools\KirinGopPinB.efi
tools\KirinBootV6.efi
:END
