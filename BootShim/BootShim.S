_Head:
  adr   x1, _Payload    /* Set _Entry address */
  b     _Start          /* Jump to the real code */

#if REQUIRES_KERNEL_HEADER == 1
#if defined(LINUX_HEADER)
  /* negroni ABL: a sane Linux arm64 header (text_offset 0, real size, 4K pages, place anywhere) */
_StackBase:
  .quad 0               /* Text Offset */

_StackSize:
  .quad FD_SIZE + 0x10000 /* Image Size */
  .quad 0xa             /* Flags */
#else
_StackBase:
  .quad FD_BASE         /* Text Offset */

_StackSize:
  .quad FD_SIZE         /* Image Size */
  .quad 0               /* Flags */
#endif
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .quad 0               /* Reserved */
  .ascii "ARM\x64"      /* ARM64 Magic */
  .long 0               /* Reserved */
#endif

_Start:
#if defined(SHIM_POWEROFF)
  /* liveness test without display: PSCI SYSTEM_OFF */
  ldr   x0, =0x84000008
  smc   #0
#endif
#if defined(GUNYAH_WDT_OFF)
  /* negroni: Gunyah virtual watchdog control (0x86000005, arg 2 = disable); ABL leaves it running */
  mov   x7, x0
  mov   x8, x1
  ldr   x0, =0x86000005
  mov   x1, #2
  mov   x2, #0
  mov   x3, #0
  smc   #0
#if defined(SHIM_STRIPE)
  /* liveness: 64 white lines at the top of the splash framebuffer */
  ldr   x4, =0xB8000000
  ldr   x6, =(1080 * 4 * 64)
  mov   x2, #-1
1:
  str   x2, [x4], #8
  subs  x6, x6, #8
  b.ne  1b
  dsb   sy
#endif
  mov   x0, x7
  mov   x1, x8
#endif
  mov   x4, x1
  ldr   x5, =FD_BASE
  cmp   x4, x5
  beq   _Entry
  ldr   x6,  =FD_SIZE

_CopyLoop:
  ldp   x2, x3, [x4], #16
  stp   x2, x3, [x5], #16
  subs  x6, x6, #16
  b.ne  _CopyLoop
  ldr   x5, =FD_BASE

_Entry:
  br    x5

_Dead:
  b     _Dead

.text
.align 4

_Payload:
