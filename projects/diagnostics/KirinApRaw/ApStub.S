// Entry point given to PSCI CPU_ON. Runs on the secondary CPU exactly as the
// secure firmware delivers it (MMU off, no stack). x0 = pointer to the report.
// Reads registers, stores them, then switches itself off again (PSCI CPU_OFF).
// Offset 0 is a progress marker so a trapping read can be located.

  .text
  .balign 64
  .global KirinApRawEntry
KirinApRawEntry:
  mov   x19, x0
  mov   x1, #1
  str   x1, [x19, #0]              // 1: entered
  mrs   x1, CurrentEL
  str   x1, [x19, #8]
  mrs   x1, mpidr_el1
  str   x1, [x19, #16]
  mrs   x1, daif
  str   x1, [x19, #24]
  mrs   x1, cntfrq_el0
  str   x1, [x19, #96]
  mrs   x2, CurrentEL
  lsr   x2, x2, #2
  cmp   x2, #2
  b.ne  9f
  mov   x1, #2
  str   x1, [x19, #0]              // 2: reading EL2 registers
  mrs   x1, sctlr_el2
  str   x1, [x19, #32]
  mrs   x1, hcr_el2
  str   x1, [x19, #40]
  mrs   x1, cnthctl_el2
  str   x1, [x19, #48]
  mrs   x1, cntvoff_el2
  str   x1, [x19, #56]
  mrs   x1, cptr_el2
  str   x1, [x19, #64]
  mrs   x1, mdcr_el2
  str   x1, [x19, #72]
  mrs   x1, hstr_el2
  str   x1, [x19, #80]
  mrs   x1, vbar_el2
  str   x1, [x19, #88]
  mrs   x1, cntv_ctl_el0
  str   x1, [x19, #104]
  mrs   x1, cntp_ctl_el0
  str   x1, [x19, #112]
  mrs   x1, cnthp_ctl_el2
  str   x1, [x19, #120]
  mov   x1, #3
  str   x1, [x19, #0]              // 3: about to read ICC_SRE_EL2 (may trap to EL3)
  mrs   x1, S3_4_C12_C9_5          // ICC_SRE_EL2
  str   x1, [x19, #128]
9:
  mov   x1, #0x100
  str   x1, [x19, #0]              // 0x100: finished
  mov   x0, #0x0002                // PSCI CPU_OFF = 0x84000002
  movk  x0, #0x8400, lsl #16
  smc   #0
1:
  wfi
  b     1b
  .global KirinApRawEnd
KirinApRawEnd:
