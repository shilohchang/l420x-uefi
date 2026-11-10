@echo -off
echo ==============================
echo L420x GOP V3 test - manual
echo ==============================
map -r
if not exist fs8:\EFI\BOOT\tools\KirinGopCompatV3.efi then
  echo ERROR V3_DRIVER_NOT_FOUND_ON_FS8
  goto END
endif
if not exist fs8:\EFI\BOOT\tools\KirinGopDiag.efi then
  echo ERROR GOP_DIAG_NOT_FOUND_ON_FS8
  goto END
endif
echo BEFORE_LOAD
fs8:\EFI\BOOT\tools\KirinGopDiag.efi > fs8:\gop-v3-before.txt
echo LOADING_V3
load fs8:\EFI\BOOT\tools\KirinGopCompatV3.efi
echo LOAD_RETURNED
echo AFTER_LOAD
fs8:\EFI\BOOT\tools\KirinGopDiag.efi > fs8:\gop-v3-after.txt
echo CHECK_LOGS_THEN_MANUALLY_BOOT_WINDOWS
:END
pause
