OUTPUT_ARCH(riscv)
ENTRY(_start)

/* QEMU + OpenSBI loads the next-stage (S-mode) at 0x80200000 by convention. */
MEMORY
{
  ram (rwx) : ORIGIN = 0x80200000, LENGTH = 8M
}

SECTIONS
{
  . = ORIGIN(ram);

  .text : ALIGN(4K)
  {
    KEEP(*(.text.start))
    *(.text .text.*)
  } > ram

  .rodata : ALIGN(4K)
  {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
  } > ram

  .data : ALIGN(4K)
  {
    *(.sdata .sdata.*)
    *(.data .data.*)
  } > ram

  .bss (NOLOAD) : ALIGN(4K)
  {
    __bss_start = .;
    *(.sbss .sbss.*)
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  } > ram
  
  /* Reserve a small kernel stack (64 KiB). You can grow this. */
  .stack (NOLOAD) : ALIGN(16)
  {
    __stack_bottom = .;
    . = . + 64K;
    __stack_top = .;
  } > ram

  /* For convenience symbols */
  PROVIDE(kernel_phys_start = ORIGIN(ram));
  PROVIDE(kernel_phys_end   = .);
}

