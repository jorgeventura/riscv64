#include <stdint.h>
#include "printf.h"

extern void enable_paging_and_jump(void (*virt_entry)(void)); // add later if desired


void kmain(uint64_t boot_info)
{
  (void)boot_info;
  printf("[riscv-vm] hello from S-mode (paging off)\n");
  printf("You can set breakpoints at kmain() now.\n");

  /* --- Place your paging setup here later ---
   * Example (when you add it):
   *   enable_paging_and_jump(virt_entry);
   */
  enable_paging_and_jump(NULL);

  /* Idle loop so you can single-step. */
  for(;;) asm volatile("wfi");
}

