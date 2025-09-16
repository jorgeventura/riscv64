OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
  u (rwx) : ORIGIN = 0x40000000, LENGTH = 256K
}

SECTIONS {
  . = ORIGIN(u);
  .text : ALIGN(4K) 
  {
  	KEEP(*(.text.start)) *(.text .text.*)
  } > u

  .rodata : ALIGN(4K)
  { 
  	*(.rodata .rodata.*)
  } > u

  .data : ALIGN(4K)
  {
	*(.data .data.*)
  } > u

  .bss (NOLOAD) : ALIGN(4K)
  {
	*(.bss .bss.*) *(COMMON)
  } > u

  .ustack (NOLOAD) : ALIGN(16)
  {
	__ustack_bottom = .; . = . + 32K; __ustack_top = .;
  } > u
}

