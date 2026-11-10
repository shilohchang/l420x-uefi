#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC UINT64 ReadMpidr (VOID) {
  UINT64 Value;
  __asm__ __volatile__ ("mrs %0, mpidr_el1" : "=r" (Value));
  return Value;
}

STATIC UINT64 ReadCntfrq (VOID) {
  UINT64 Value;
  __asm__ __volatile__ ("mrs %0, cntfrq_el0" : "=r" (Value));
  return Value;
}

STATIC UINT64 ReadCntvct (VOID) {
  UINT64 Value;
  __asm__ __volatile__ ("isb\n\tmrs %0, cntvct_el0" : "=r" (Value));
  return Value;
}

EFI_STATUS EFIAPI UefiMain (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  UINT64 Mpidr, Cntfrq, A, B;
  EFI_STATUS Status;
  Mpidr  = ReadMpidr ();
  Cntfrq = ReadCntfrq ();
  A = ReadCntvct ();
  Status = gBS->Stall (100000); // 100 ms, firmware provided delay
  B = ReadCntvct ();

  Print (L"=== Kirin9000C HardwareProbe v1 (READ ONLY) ===\n");
  Print (L"Current CPU only (UEFI boot processor)\n");
  Print (L"MPIDR_EL1  : 0x%016lx\n", Mpidr);
  Print (L"Affinity   : 0x%010lx\n", Mpidr & 0xFF00FFFFFFULL);
  Print (L"CNTFRQ_EL0 : %lu Hz\n", Cntfrq);
  Print (L"CNTVCT 1   : 0x%016lx\n", A);
  Print (L"CNTVCT 2   : 0x%016lx\n", B);
  Print (L"Stall status: %r\n", Status);
  Print (L"Counter delta during ~100ms: %lu\n", B - A);
  Print (L"No register writes, no disk or NVRAM writes.\n");
  Print (L"Press any key to exit.\n");
  EFI_INPUT_KEY Key;
  UINTN Index;
  gBS->WaitForEvent (1, &SystemTable->ConIn->WaitForKey, &Index);
  SystemTable->ConIn->ReadKeyStroke (SystemTable->ConIn, &Key);
  return EFI_SUCCESS;
}
