// SPDX-License-Identifier: BSD-2-Clause-Patent

#include <AsmMacroLib.h>

ASM_FUNC(NegroniEnterXnu)
  msr   daifset, #15

  mrs   x6, ctr_el0
  ubfx  x6, x6, #16, #4
  mov   x7, #4
  lsl   x6, x7, x6
  sub   x7, x6, #1
  bic   x4, x2, x7
  add   x5, x2, x3
1:
  dc    civac, x4
  add   x4, x4, x6
  cmp   x4, x5
  b.lo  1b
  dsb   sy
  ic    iallu
  dsb   sy
  isb

  mrs   x4, sctlr_el1
  bic   x4, x4, #1
  bic   x4, x4, #4
  bic   x4, x4, #0x1000
  msr   sctlr_el1, x4
  isb
  tlbi  vmalle1
  dsb   sy
  isb
  br    x1
