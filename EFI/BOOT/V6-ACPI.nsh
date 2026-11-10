@echo -off
# Experimental ACPI profile, only after a full power-off.
fs8:
if not exist fs8:\EFI\BOOT\tools\AcpiTableDxe.efi then
  echo ERROR: AcpiTableDxe.efi missing
  goto END
endif
if not exist fs8:\EFI\BOOT\tools\KirinAcpiSessionLoaderAuto.efi then
  echo ERROR: session loader missing
  goto END
endif
if not exist fs8:\EFI\BOOT\tools\KirinBootV6.efi then
  echo ERROR: KirinBootV6.efi missing
  goto END
endif
if not exist fs8:\EFI\BOOT\logs then
  mkdir fs8:\EFI\BOOT\logs
endif
load fs8:\EFI\BOOT\tools\AcpiTableDxe.efi > fs8:\EFI\BOOT\logs\v6-acpi-driver.txt
if not %lasterror% == 0 then
  echo ERROR: ACPI driver load
  goto END
endif
fs8:\EFI\BOOT\tools\KirinAcpiSessionLoaderAuto.efi > fs8:\EFI\BOOT\logs\v6-acpi-install.txt
if not %lasterror% == 0 then
  echo ERROR: ACPI install
  goto END
endif
fs8:\EFI\BOOT\tools\KirinBootV6.efi
:END
