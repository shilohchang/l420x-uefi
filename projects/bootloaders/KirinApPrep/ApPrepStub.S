// Entry point given to PSCI CPU_ON. Runs on a secondary CPU as delivered by the
// secure firmware (EL2, MMU off, no stack). x0 = pointer to an 8-slot report.
// Records HCR_EL2, replaces it with a clean value, records it again, then
// switches the CPU off (PSCI CPU_OFF).
//
// Clean value: only RW (bit 31) = "EL1 is AArch64". In particular this clears
//   VM  (bit 0)  - stage-2 translation, with no valid tables behind it
//   TSC (bit 19) - trapping of the OS's own SMC calls into EL2

  .text
  .balign 64
  .global KirinApPrepEntry
KirinApPrepEntry:
  mov   x19, x0
  mov   x1, #1
  str   x1, [x19, #0]              // 1: entered
  mrs   x1, CurrentEL
  str   x1, [x19, #8]
  mrs   x1, mpidr_el1
  str   x1, [x19, #16]
  mrs   x2, CurrentEL
  lsr   x2, x2, #2
  cmp   x2, #2
  b.ne  9f
  mrs   x1, hcr_el2
  str   x1, [x19, #24]             // HCR_EL2 as delivered
  mrs   x1, vbar_el2
  str   x1, [x19, #40]
  mov   x1, #0x80000000
  msr   hcr_el2, x1
  msr   cntvoff_el2, xzr
  isb
  mrs   x1, hcr_el2
  str   x1, [x19, #32]             // HCR_EL2 after cleaning
9:
  mov   x1, #0x100
  str   x1, [x19, #0]              // 0x100: finished
  mov   x0, #0x0002                // PSCI CPU_OFF = 0x84000002
  movk  x0, #0x8400, lsl #16
  smc   #0
1:
  wfi
  b     1b
  .global KirinApPrepEnd
KirinApPrepEnd:
