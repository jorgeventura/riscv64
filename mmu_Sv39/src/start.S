/* Build with: -march=rv64gc -mabi=lp64 */
.section .text.start
.globl _start
_start:
  /* We assume OpenSBI already put us in S-mode with MMU off (Bare). */

  /* Set up stack */
  la sp, __stack_top

  /* Zero .bss */
  la   t0, __bss_start
  la   t1, __bss_end
1:
  bgeu t0, t1, 2f
  sd   zero, 0(t0)
  addi t0, t0, 8
  j    1b
2:

  /* Call C entry */
  la   a0, boot_info         /* optional placeholder */
  call kmain

halt:
  wfi
  j halt

/* Minimal placeholders the linker script provides */
.extern __bss_start
.extern __bss_end
.extern __stack_top

/* Provide a tiny symbol the C can see for testing */
.section .rodata
.globl boot_info
boot_info:
  .quad 0x1122334455667788

