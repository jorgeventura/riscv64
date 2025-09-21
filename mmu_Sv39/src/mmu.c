// types.h-ish
#include <stdint.h>
#define PGSIZE 4096UL
#define PTE_V (1UL<<0)
#define PTE_R (1UL<<1)
#define PTE_W (1UL<<2)
#define PTE_X (1UL<<3)
#define PTE_U (1UL<<4)
#define PTE_G (1UL<<5)
#define PTE_A (1UL<<6)
#define PTE_D (1UL<<7)

#define SATP_MODE_SV39 (8UL << 60)
static inline void sfence_vma_all(void){ asm volatile("sfence.vma x0, x0" ::: "memory"); }

// Simple helpers for Sv39 indexing
static inline int vpn0(uint64_t va){ return (va >> 12) & 0x1ff; }
static inline int vpn1(uint64_t va){ return (va >> 21) & 0x1ff; }
static inline int vpn2(uint64_t va){ return (va >> 30) & 0x1ff; }
static inline uint64_t pte_make(uint64_t ppn, uint64_t flags){ return (ppn << 10) | flags; }

// Page tables (very small demo, statically reserved)
static uint64_t __attribute__((aligned(PGSIZE))) L2[512]; // root
static uint64_t __attribute__((aligned(PGSIZE))) L1_hi[512];
static uint64_t __attribute__((aligned(PGSIZE))) L1_id[512];

extern char kernel_phys_start[];
extern char kernel_phys_end[];
#define KERNEL_PHYS_BASE ((uint64_t)kernel_phys_start)
#define KERNEL_SIZE      ((uint64_t)(kernel_phys_end - kernel_phys_start))
// KERNEL_VIRT_BASE 0xffff_ffc0_0000_0000UL  // example in Sv39 high half
#define KERNEL_VIRT_BASE 0xffffffc000000000UL  // example in Sv39 high half

// Map a 1GiB superpage at level 2 (if aligned), else fall back to 2MiB pages, etc.
// For simplicity, this demo maps with 2MiB pages at L1.
static void map_2m_region(uint64_t L2tbl[], uint64_t L1tbl[],
                          uint64_t va_base, uint64_t pa_base,
                          uint64_t length, uint64_t flags)
{
    // Install L1 pointer in L2
    int i2 = vpn2(va_base);
    uint64_t l1_ppn = ((uint64_t)L1tbl) >> 12;
    L2tbl[i2] = pte_make(l1_ppn, PTE_V); // pointer PTE (no R/W/X)

    // Fill L1 leaf entries for 2MiB pages
    uint64_t pages2m = (length + (2UL<<20) - 1) / (2UL<<20);
    for(uint64_t i=0;i<pages2m;i++){
        uint64_t va = va_base + i*(2UL<<20);
        int i1 = vpn1(va);
        uint64_t ppn_2m = (pa_base + i*(2UL<<20)) >> 12;
        // Level-1 leaf with R/W/X
        L1tbl[i1] = pte_make(ppn_2m, flags | PTE_V | PTE_A | PTE_D | PTE_R | PTE_W | PTE_X);
    }
}

void enable_paging_and_jump(void (*virt_entry)(void))
{
    // Zero tables
    for(int i=0;i<512;i++){ L2[i]=0; L1_hi[i]=0; L1_id[i]=0; }

    // 1) Identity map a small window covering our current code/stack (say 4 MiB)
    const uint64_t id_pa = (uint64_t)kernel_phys_start & ~((2UL<<20)-1);
    const uint64_t id_len = 4UL<<20; // keep small
    map_2m_region(L2, L1_id, id_pa, id_pa, id_len, /*flags*/0);

    // 2) Higher-half map the kernel image
    const uint64_t k_pa = KERNEL_PHYS_BASE & ~((2UL<<20)-1);
    const uint64_t k_len = (KERNEL_SIZE + ((KERNEL_PHYS_BASE - k_pa))) ;
    map_2m_region(L2, L1_hi, KERNEL_VIRT_BASE, k_pa, k_len, /*flags*/0);

    // Install the second L1 as well (this simplistic demo reuses the same L2 slot if both share vpn2)
    // In practice choose VA ranges that don’t collide at vpn2; otherwise use separate L2 slots.

    // Write satp with root L2’s PPN
    uint64_t root_ppn = ((uint64_t)L2) >> 12;
    uint64_t satp_val = SATP_MODE_SV39 | (0ULL << 44) /*ASID*/ | root_ppn;
    asm volatile("csrw satp, %0" :: "r"(satp_val));
    sfence_vma_all();

    // Now paging is on. Jump to the virtual entry (must be mapped).
    virt_entry();
}

