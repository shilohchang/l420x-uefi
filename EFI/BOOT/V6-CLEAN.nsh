@echo -off
# CLEAN profile. No ACPI or GOP driver loaded by this script.
# Verify fs8 is the USB containing pe.efi before launching.
fs8:
if not exist fs8:\EFI\BOOT\pe.efi then
  echo ERROR: pe.efi missing from fs8
  goto END
endif
if not exist fs8:\EFI\BOOT\tools\KirinBootV6.efi then
  echo ERROR: KirinBootV6.efi missing
  goto END
endif
fs8:\EFI\BOOT\tools\KirinBootV6.efi
:END
