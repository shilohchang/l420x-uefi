@echo -off
fs8:
if not exist fs8:\efi\boot\logs then
  mkdir fs8:\efi\boot\logs
endif
if not exist fs8:\efi\boot\tools\KirinKdProbe.efi then
  echo ERROR: Missing KirinKdProbe.efi
  goto DONE
endif
echo [KD7] Checking published ACPI and debug interfaces only
echo [KD7] This script does not inject ACPI or boot Windows
fs8:\efi\boot\tools\KirinKdProbe.efi > fs8:\efi\boot\logs\v7-kdprobe.txt
type fs8:\efi\boot\logs\v7-kdprobe.txt
:DONE
echo [KD7] Finished
