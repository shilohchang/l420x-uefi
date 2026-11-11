@echo -off
# GOP diagnostic. Run from the USB volume:  fsN:  then  cd \EFI\BOOT  then  GOPDIAG.nsh
# Optional colour-bar test:  GOPDIAG.nsh /bars
# Output goes to the screen and is appended to \EFI\BOOT\logs\GOPDIAG.TXT by the program itself.
if not exist tools\KirinGopDiagV3.efi then
  echo ERROR: tools\KirinGopDiagV3.efi not found. cd to \EFI\BOOT on the USB volume first.
  goto END
endif
if not exist logs then
  mkdir logs
endif
tools\KirinGopDiagV3.efi %1
echo Done. Log: \EFI\BOOT\logs\GOPDIAG.TXT
:END
