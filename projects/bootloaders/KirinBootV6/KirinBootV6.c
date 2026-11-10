/** L420X V6 Windows PE chainloader: stock GOP only, persistent pre-boot logs. */
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DevicePath.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>

STATIC EFI_FILE_PROTOCOL *LogFile = NULL;
STATIC EFI_HANDLE DeviceHandle = NULL;

STATIC VOID Trace(CONST CHAR8 *Tag, EFI_STATUS Status) {
  CHAR8 Buf[192];
  UINTN Len = AsciiSPrint(Buf, sizeof(Buf), "[V6] %a status=0x%lx\r\n", Tag, (UINT64)Status);
  Print(L"[V6] %a status=%r (0x%lx)\n", Tag, Status, (UINT64)Status);
  if (LogFile != NULL) {
    LogFile->Write(LogFile, &Len, Buf);
    LogFile->Flush(LogFile);
  }
}
STATIC VOID TraceValue(CONST CHAR8 *Tag, UINT64 V1, UINT64 V2) {
  CHAR8 Buf[192];
  UINTN Len = AsciiSPrint(Buf, sizeof(Buf), "[V6] %a 0x%lx 0x%lx\r\n", Tag, V1, V2);
  Print(L"[V6] %a 0x%lx 0x%lx\n", Tag, V1, V2);
  if (LogFile != NULL) {
    LogFile->Write(LogFile, &Len, Buf);
    LogFile->Flush(LogFile);
  }
}
STATIC EFI_STATUS OpenLog(EFI_HANDLE ImageHandle) {
  EFI_STATUS S;
  EFI_LOADED_IMAGE_PROTOCOL *Li;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL *Root = NULL, *Efi = NULL, *Boot = NULL, *Logs = NULL;
  S = gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Li);
  if (EFI_ERROR(S)) return S;
  DeviceHandle = Li->DeviceHandle;
  S = gBS->HandleProtocol(DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR(S)) return S;
  S = Fs->OpenVolume(Fs, &Root);
  if (EFI_ERROR(S)) return S;
  S = Root->Open(Root, &Efi, L"EFI", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (EFI_ERROR(S)) goto Done;
  S = Efi->Open(Efi, &Boot, L"BOOT", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
  if (EFI_ERROR(S)) goto Done;
  S = Boot->Open(Boot, &Logs, L"logs", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
  if (EFI_ERROR(S)) goto Done;
  S = Logs->Open(Logs, &LogFile, L"V6-TRACE.TXT", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (!EFI_ERROR(S)) S = LogFile->SetPosition(LogFile, MAX_UINT64);
Done:
  if (Logs != NULL) Logs->Close(Logs);
  if (Boot != NULL) Boot->Close(Boot);
  if (Efi != NULL) Efi->Close(Efi);
  if (Root != NULL) Root->Close(Root);
  return S;
}
STATIC VOID DumpAcpi(VOID) {
  UINTN I;
  for (I=0; I<gST->NumberOfTableEntries; ++I) {
    EFI_CONFIGURATION_TABLE *T = &gST->ConfigurationTable[I];
    if (CompareGuid(&T->VendorGuid, &gEfiAcpi20TableGuid)) {
      TraceValue("ACPI_20_RSDP", (UINTN)T->VendorTable, 0);
    }
    if (CompareGuid(&T->VendorGuid, &gEfiAcpi10TableGuid)) {
      TraceValue("ACPI_10_RSDP", (UINTN)T->VendorTable, 0);
    }
  }
}
STATIC VOID DumpGop(VOID) {
  EFI_STATUS S;
  EFI_HANDLE *Handles = NULL;
  UINTN Count = 0, I;
  S = gBS->LocateHandleBuffer(ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                              NULL, &Count, &Handles);
  Trace("LocateAllGOP", S);
  if (EFI_ERROR(S)) return;
  TraceValue("GOP_Count", Count, 0);
  for (I = 0; I < Count; ++I) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
    EFI_DEVICE_PATH_PROTOCOL *Dp = NULL;
    TraceValue("GOP_Handle_Index", I, (UINTN)Handles[I]);
    S = gBS->HandleProtocol(Handles[I], &gEfiDevicePathProtocolGuid, (VOID **)&Dp);
    Trace("GOP_DevicePath", S);
    S = gBS->HandleProtocol(Handles[I], &gEfiGraphicsOutputProtocolGuid, (VOID **)&Gop);
    Trace("GOP_HandleProtocol", S);
    if (EFI_ERROR(S) || Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL) continue;
    TraceValue("GOP_Mode_Max", Gop->Mode->Mode, Gop->Mode->MaxMode);
    TraceValue("GOP_Resolution", Gop->Mode->Info->HorizontalResolution,
               Gop->Mode->Info->VerticalResolution);
    TraceValue("GOP_PixelFormat_Stride", Gop->Mode->Info->PixelFormat,
               Gop->Mode->Info->PixelsPerScanLine);
    TraceValue("GOP_FB_Base_Size", Gop->Mode->FrameBufferBase,
               Gop->Mode->FrameBufferSize);
    // Do not call SetMode, Blt, or write to framebuffer.
  }
  if (Handles != NULL) FreePool(Handles);
}
STATIC VOID DumpMemoryMap(VOID) {
  EFI_STATUS S;
  EFI_MEMORY_DESCRIPTOR *Map = NULL, *D;
  UINTN Size = 0, Key=0, DescSize=0, Count=0, I;
  UINT32 Version=0;
  S = gBS->GetMemoryMap(&Size, Map, &Key, &DescSize, &Version);
  Trace("GetMemoryMap_probe", S);
  if (S != EFI_BUFFER_TOO_SMALL || DescSize < sizeof(EFI_MEMORY_DESCRIPTOR)) return;
  Size += 16 * DescSize + 4096;
  Map = AllocatePool(Size);
  if (Map == NULL) { Trace("MemoryMap_Allocate", EFI_OUT_OF_RESOURCES); return; }
  S = gBS->GetMemoryMap(&Size, Map, &Key, &DescSize, &Version);
  Trace("GetMemoryMap_actual", S);
  if (!EFI_ERROR(S) && DescSize) {
    Count = Size / DescSize;
    TraceValue("MemoryMap_Count_Version", Count, Version);
    for (I = 0; I < Count; ++I) {
      D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + I * DescSize);
      // Dump runtime and the memory descriptor containing the known L420x framebuffer.
      if ((D->Attribute & EFI_MEMORY_RUNTIME) ||
          (D->PhysicalStart <= 0x51DD0000ULL &&
           D->NumberOfPages > 0 &&
           (0x51DD0000ULL - D->PhysicalStart) / 4096 < D->NumberOfPages)) {
        TraceValue("MAP_Type_Addr", D->Type, D->PhysicalStart);
        TraceValue("MAP_Pages_Attrs", D->NumberOfPages, D->Attribute);
      }
    }
  }
  FreePool(Map);
}
EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS S;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_HANDLE Child = NULL;
  EFI_TIME Now;
  UINTN ExitDataSize = 0;
  CHAR16 *ExitData = NULL;
  Print(L"\nL420X V6 CHAINLOADER - READ ONLY FIRMWARE SNAPSHOT\n");
  S = OpenLog(ImageHandle);
  Print(L"V6 log open: %r\n", S);
  Trace("START_NEW_RUN", EFI_SUCCESS);
  TraceValue("UEFI_Revision", gST->Hdr.Revision, gST->FirmwareRevision);
  TraceValue("BootServicesPtr", (UINTN)gBS, (UINTN)gRT);
  if (gRT != NULL && !EFI_ERROR(gRT->GetTime(&Now, NULL))) {
    TraceValue("UTC_TIME_YYYYMMDD", Now.Year * 10000ULL + Now.Month * 100 + Now.Day,
               Now.Hour * 10000ULL + Now.Minute * 100 + Now.Second);
  }
  DumpAcpi();
  DumpGop();
  DumpMemoryMap();
  if (DeviceHandle == NULL) { Trace("NO_VOLUME_DEVICE", EFI_NOT_FOUND); return EFI_NOT_FOUND; }
  Path = FileDevicePath(DeviceHandle, L"\\EFI\\BOOT\\pe.efi");
  if (Path == NULL) { Trace("FileDevicePath", EFI_OUT_OF_RESOURCES); return EFI_OUT_OF_RESOURCES; }
  Trace("BEFORE_LoadImage", EFI_SUCCESS);
  S = gBS->LoadImage(FALSE, ImageHandle, Path, NULL, 0, &Child);
  Trace("AFTER_LoadImage", S);
  FreePool(Path);
  if (EFI_ERROR(S)) goto End;
  Trace("BEFORE_StartImage", EFI_SUCCESS);
  Print(L"[V6] HANDOFF TO WINDOWS PE NOW\n");
  S = gBS->StartImage(Child, &ExitDataSize, &ExitData);
  // This executes only if PE returns control. Not executed on a successful Windows handoff.
  Trace("AFTER_StartImage_returned", S);
  if (ExitData != NULL) {
    Print(L"[V6] ExitData: %s\n", ExitData);
    FreePool(ExitData);
  }
  if (Child != NULL) gBS->UnloadImage(Child);
End:
  Trace("V6_FINISHED", S);
  if (LogFile != NULL) { LogFile->Flush(LogFile); LogFile->Close(LogFile); }
  return S;
}
