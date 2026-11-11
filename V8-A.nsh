@echo -off
# V8.2-A: ACPI + pinned GOP (variant A) + display guard + Windows PE.
# Put this file in the ROOT of the USB volume. After a COLD boot:  fsN:  then  V8-A.nsh
if not exist \EFI\BOOT\tools\AcpiTableDxe.efi then
  echo ERROR: \EFI\BOOT\tools\AcpiTableDxe.efi missing (switch to the USB volume first, e.g. fs8:)
  goto END
endif
if not exist \EFI\BOOT\tools\KirinAcpiSessionLoaderAuto.efi then
  echo ERROR: KirinAcpiSessionLoaderAuto.efi missing
  goto END
endif
if not exist \EFI\BOOT\tools\KirinGopPinA.efi then
  echo ERROR: KirinGopPinA.efi missing
  goto END
endif
if not exist \EFI\BOOT\tools\KirinEbsGuard.efi then
  echo ERROR: KirinEbsGuard.efi missing
  goto END
endif
if not exist \EFI\BOOT\tools\KirinBootV6.efi then
  echo ERROR: KirinBootV6.efi missing
  goto END
endif
if not exist \EFI\BOOT\pe.efi then
  echo ERROR: \EFI\BOOT\pe.efi missing (copy of bootmgfw.efi that KirinBootV6 starts)
  goto END
endif
load \EFI\BOOT\tools\AcpiTableDxe.efi
\EFI\BOOT\tools\KirinAcpiSessionLoaderAuto.efi
if not %lasterror% == 0 then
  echo ERROR: ACPI install failed - cold boot and retry
  goto END
endif
load -nc \EFI\BOOT\tools\KirinGopPinA.efi
load -nc \EFI\BOOT\tools\KirinEbsGuard.efi
\EFI\BOOT\tools\KirinBootV6.efi
:END
