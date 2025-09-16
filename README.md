# riscv64

Perfect — let’s draw how your three arrays (`L2`, `L1_id`, `L1_hi`) are actually used by the Sv39 page walker.

---

## 1. Sv39 VA breakdown

In Sv39, a **64-bit virtual address** is divided as:

```
|63 .......... 39|38 .......... 30|29 .......... 21|20 .......... 12|11 ...... 0|
    sign bits         VPN[2]            VPN[1]          VPN[0]          page offset
```

* **VPN[2]** → index into **L2** (root) table
* **VPN[1]** → index into **L1** table
* **VPN[0]** → index into **L0** table (or use a superpage entry at L1/L2)
* **page offset** → offset within the 4 KiB page

---

## 2. Your tables in context

You have **one L2 (root)** and two subordinate L1 tables:

```
L2  (root, 512 entries)
│
├─► pointer to L1_id  (for identity maps: VA ≈ PA)
│
└─► pointer to L1_hi  (for higher-half kernel maps: VA ≈ 0xffff_ffc0_... )
```

Each `L1` has 512 entries; each entry can be:

* A **leaf PTE** (maps a 2 MiB “superpage” if you stop at L1, or a 4 KiB page if you go down one more level).
* Or a **pointer PTE** (to an L0 table, if you want fine-grained 4 KiB mappings).

---

## 3. ASCII diagram

```
Virtual Address (Sv39)               Page Table Walk
──────────────────────────────────────────────────────────────
VA[38:30]  (9 bits)  ──► index into L2[512]
                      │
                      ├── if points to L1_id: used for identity region
                      │       VA[29:21] ──► index into L1_id[512]
                      │                         └─ leaf PTE maps PA directly
                      │
                      └── if points to L1_hi: used for higher-half region
                              VA[29:21] ──► index into L1_hi[512]
                                                └─ leaf PTE maps PA+offset

VA[20:12] is only used if you have an L0 table. In this simple setup,
we map large 2 MiB pages at L1, so the walk usually stops at L1.
```

---

## 4. Example: Identity mapping

Say you want VA `0x0000_0000_8020_0000` → PA `0x0000_0000_8020_0000`.

1. Extract VA[38:30] → index into `L2`.
2. That entry points to `L1_id`.
3. Use VA[29:21] to select a PTE in `L1_id`.
4. That entry is a **leaf**, giving PPN (physical page number) for 2 MiB superpage.
5. Combine PPN with VA[20:12] + page offset.

Result: VA == PA.

---

## 5. Example: Higher-half mapping

Say you want VA `0xffff_ffc0_8020_0000` → PA `0x0000_0000_8020_0000`.

1. Extract VA[38:30] (these high addresses are sign-extended, so top 25 bits are 1).
2. That index in `L2` points to `L1_hi`.
3. Use VA[29:21] to select entry in `L1_hi`.
4. Leaf entry → points to the same PA as above.

Result: Kernel sees itself in higher-half VA, but hardware fetches instructions/data from low PA.

---

## 6. Why two subordinate tables?

* `L1_id`: keep identity mappings alive while you flip `satp`. Without this, as soon as you enable paging the CPU would jump into nowhere.
* `L1_hi`: where you really want the kernel to live once paging is on (high canonical addresses).

---

✅ **Summary**

* `L2[512]` is the Sv39 root page table.
* `L1_id[512]` = subordinate L1 for identity mappings.
* `L1_hi[512]` = subordinate L1 for higher-half mappings.
* VA[38:30] selects the slot in L2. That slot either points to L1_id or L1_hi.
* VA[29:21] selects the slot in the L1, which holds a leaf mapping (2 MiB pages in your demo).

---

## 1. The virtual address bits in Sv39

A 64-bit VA is divided like this:

```
|63............39|38.......30|29.......21|20.......12|11......0|
 sign extension   VPN[2]       VPN[1]      VPN[0]     page offset
```

* **Page offset (11..0)** → selects byte inside a 4 KiB page.
* **VPN[0..2] (30..12 bits)** → 27 bits total, used to index page tables.
* **Bits 63..39** must be a **sign extension** of bit 38 (canonical addresses).

---

## 2. Size of the Sv39 virtual space

* VPN[2:0] = 27 bits → up to `2^27 = 134,217,728` entries.
* Each entry maps one 4 KiB page → total = `2^27 × 4 KiB = 2^39 = 512 GiB`.

So yes: the usable virtual address space in Sv39 is **512 GiB** (256 GiB “low half” + 256 GiB “high half”).
That matches what you wrote: `0x8000_0000_00` = 512 GiB.

---

## 3. What about bits 63..39?

They are not part of the translation walk in Sv39.
Instead, they must be a **sign-extension** of bit 38:

* If VA[38] = 0 → bits 63..39 must all be 0. → **low half**: `0x0000_0000_0000_0000 – 0x0000_003F_FFFF_FFFF`.
* If VA[38] = 1 → bits 63..39 must all be 1. → **high half**: `0xFFFF_FFC0_0000_0000 – 0xFFFF_FFFF_FFFF_FFFF`.

Any address that is not canonical (wrong sign extension) raises a page fault.

---

## 4. “But I don’t have 512 GiB of RAM…”

Correct. That 512 GiB is **virtual space** only.
Your real physical RAM is much smaller (e.g., 4 MiB, 4 GiB, …).
Virtual addresses are just names — you can choose where to map them.

---

## 5. When does VA → PA mapping happen?

When you enable paging:

1. You write the **physical address of the root page table** into `satp`.
2. CPU sets `MODE=Sv39` in `satp`.
3. On the next memory access, hardware walks the page tables:

   * Take VA bits [38:30], index into L2.
   * Take VA bits [29:21], index into L1.
   * Take VA bits [20:12], index into L0 (if used).
   * Read the PPN (physical page number) from the leaf PTE.
   * Concatenate with page offset → **physical address**.

That’s the exact moment the mapping happens: on every load/store/fetch after paging is on.

---

✅ **Summary**

* Sv39 gives you 512 GiB of virtual address space, split into canonical low and high halves.
* Bits 63..39 are just sign-extension of bit 38 (not extra address bits).
* Physical memory is much smaller; VA only gets meaning when page tables translate it.
* The VA→PA mapping occurs dynamically whenever the CPU accesses memory after `satp.MODE=Sv39`.

---

👉 Do you want me to show with a worked example:
Take VA `0xffff_ffc0_8020_0000`, walk through your `L2 → L1_hi` tables, and show exactly how it resolves to PA `0x0000_0000_8020_0000`?

awesome—let’s walk one concrete Sv39 translation **end-to-end** with the exact tables you sketched (L2 root → L1_hi for higher-half, using 2 MiB leaves).

We’ll translate this virtual address:

```
VA = 0xffff_ffc0_8020_0000   →   PA = 0x0000_0000_8020_0000
```

…and also show that any byte inside that 2 MiB range (e.g., `…1234`) maps to the same PA + offset.

---

# 1) Break the VA into Sv39 fields

Sv39 splits a 64-bit VA like this:

```
|63..39|38..30|29..21|20..12|11..0|
  SE    VPN2   VPN1   VPN0   page offset
```

For `VA = 0xffff_ffc0_8020_0000`:

* `VPN2 = VA[38:30] = 0x102` (decimal 258)
* `VPN1 = VA[29:21] = 0x1`
* `VPN0 = VA[20:12] = 0x0`
* `offset = VA[11:0] = 0x000`

(High bits 63..39 are just the **sign-extension** of bit 38—canonical address requirement.)

---

# 2) Your page tables (what you prepared)

* `L2` (root, 4 KiB page, 512 entries) — you load its **physical PPN** into `satp`.
* `L1_hi` — an L1 table (4 KiB, 512 entries) used for **higher-half** mappings.
* `L1_id` — an L1 table used for **identity** mappings (not used in this high-half example).

You filled:

* `L2[VPN2=0x102]` = **pointer PTE** to `L1_hi` (Valid=1, R/W/X=0 → non-leaf).
* `L1_hi[VPN1=0x1]` = **leaf PTE at level-1** mapping a **2 MiB superpage** whose base **PA = 0x0000_0000_8020_0000**.
  (Because it’s a level-1 leaf, the mapping granularity is 2 MiB, and the **PPN0** bits must be zero → PA must be 2 MiB-aligned. `0x8020_0000` is aligned: `0x20_0000` = 2 MiB.)

PTE flags on the leaf: `V=1, R/W/X as you want, plus A/D` (if you’re pre-setting them).

---

# 3) The hardware walk

1. **L2 access:**
   Use `VPN2=0x102` to index `L2`. Entry is a **pointer** to `L1_hi` (i.e., its PPN).
   → continue to next level.

2. **L1 access (higher-half table):**
   Use `VPN1=0x1` to index `L1_hi`. Entry is a **leaf** (R/W/X set) at **level-1**.
   Because it’s a level-1 leaf, the page size is **2 MiB** and the walk stops here.

3. **Form the PA:**

   * Take the PPN from that leaf (which encodes `PA_base = 0x…8020_0000`).
   * Combine with the **low 21 bits** of the VA (that’s `VPN0:offset`) as the **in-page offset**.
   * Here `VPN0=0`, `offset=0`, so result is exactly `0x8020_0000`.

If we translate **any byte inside that 2 MiB region**, e.g.:

```
VA' = 0xffff_ffc0_8020_1234
```

then the result is:

```
PA' = 0x0000_0000_8020_0000   |   (VA' & 0x1f_ffff)   =  0x8020_1234
```

Same 2 MiB leaf, just adding the **2 MiB offset** from VA.

---

# 4) Why this works without an L0 table

Because you chose **2 MiB superpages** at L1 (leaf at level-1), the walk *stops* at L1.

* If you needed 4 KiB granularity, your L1 entry would be a **pointer** to an **L0 table**, and the leaf would live at L0 (level-0).

---

# 5) Identity mapping example (quick)

For `VA = 0x0000_0000_8020_0000` (low half):

* `VPN2 = 0x2`, `VPN1 = 0x1`, `VPN0 = 0x0`.
* `L2[0x2]` → pointer to `L1_id`.
* `L1_id[0x1]` → **leaf** mapping PA base `0x8020_0000`.
* Result: `VA == PA` (useful during the switch to paging).

---

# 6) PTE alignment constraints (important)

* **Level-1 leaf (2 MiB)**: the PA must be **2 MiB-aligned** → PPN0 must be 0.
* **Level-2 leaf (1 GiB)**: the PA must be **1 GiB-aligned** → PPN1=PPN0=0.
* **Level-0 leaf (4 KiB)**: normal 4 KiB alignment.

You picked `0x8020_0000` (2 MiB-aligned), so it’s valid as an L1 leaf.

---

## TL;DR (worked mapping)

* **Given**: `VA = 0xffff_ffc0_8020_0000`
* **Indices**: `VPN2=0x102 → L2`, `VPN1=0x1 → L1_hi leaf`, `VPN0=0`
* **Leaf**: L1_hi[1] maps 2 MiB at **PA base = 0x8020_0000**
* **Result**: `PA = 0x8020_0000 (+ offset if any)`
* Example with offset: `VA=…1234 → PA=0x8020_1234`.

If you want, I can drop a tiny GDB helper (or a C debug print) that, given a VA, prints the **L2/L1 indices + whether it hits your L1_hi or L1_id table**, so you can watch the mapping live while single-stepping.

Great question—the short answer is: **because `L2` is indexed by VA bits [38:30] (VPN2), and for `0x0000_0000_8020_0000` those bits equal `2`.**

Here’s the breakdown.

# How the L2 index is computed

For Sv39:

```
VA bits: |63..39 sign|38..30 VPN2|29..21 VPN1|20..12 VPN0|11..0 offset|
Indexing:          L2          L1          L0
```

The **L2 index = VPN2 = (VA >> 30) & 0x1ff**.

Compute it for your address:

* VA = `0x0000_0000_8020_0000`
* `VA >> 30 = 0x2`
* `(… & 0x1ff) = 0x2`

So the entry is **`L2[2]`** (third slot). Intuition: each L2 slot covers a **1 GiB chunk** of the VA space. Index 0 → `[0, 1 GiB)`, index 1 → `[1 GiB, 2 GiB)`, index **2 → `[2 GiB, 3 GiB)`**, whose base is `0x8000_0000`. Your address `0x8020_0000` is inside that 1 GiB window, so L2 index is 2.

# Low-half vs high-half

* Low-half address `0x0000_0000_8020_0000` → `VPN2 = 0x2` → **`L2[2]`**.
* High-half alias `0xffff_ffc0_8020_0000` (canonical sign-extension) → `VPN2 = 0x102` → **`L2[0x102]`**.

This is why, in your code, if you build:

* an **identity map** for `0x8020_0000`, you populate `L2[2] → L1_id[...]`.
* a **higher-half map** for `0xffff_ffc0_8020_0000`, you must also populate `L2[0x102] → L1_hi[...]`.

# Handy mental model

* **L2 index** steps in **1 GiB strides**.
* **L1 index** (VPN1) steps in **2 MiB strides** (when using L1 leaves / superpages).
* **L0 index** (VPN0) steps in **4 KiB strides** (when using L0 leaves).

So your observation that `i2 == 2` at `mmu.c:42` is exactly right: `0x8020_0000` falls in the **third 1 GiB region**, hence `L2[2]`.


Totally—`pte_make()` is just a tiny helper to **pack a RISC-V PTE (page-table entry)** from a physical page number (PPN) plus flag bits.

Here’s the mental model and the exact bit layout it’s encoding.

# 1) Sv39 PTE bit layout (64-bit)

```
63......................54 53........10 9....8 7 6 5 4 3 2 1 0
[   reserved (must be 0) ] [   PPN   ] [RSW ] D A G U X W R V
```

* **V** (bit 0): valid
* **R/W/X** (1/2/3): leaf permissions (read/write/execute)
* **U** (4): user-visible (U-mode can use this mapping if set)
* **G** (5): global (not flushed on ASID changes)
* **A/D** (6/7): accessed/dirty (set by HW if supported, else by SW)
* **RSW** (8–9): “reserved for software” (free for your own use)
* **PPN** (10–53): physical page number (i.e., `PA >> 12`)
* **63–54**: reserved in base Sv39 (must be zero unless you enable optional extensions like Svpbmt/Svnapot — ignore for now)

> **Leaf vs pointer PTE**:
> If any of **R/W/X** is 1 → it’s a **leaf** (final translation).
> If **R=W=X=0** and **V=1** → it’s a **pointer** to the next level page table.

# 2) What `pte_make(ppn, flags)` does

It puts your PPN into bits 53..10 and ORs in the flags:

```c
static inline uint64_t pte_make(uint64_t ppn, uint64_t flags) {
    return (ppn << 10) | flags;  // flags occupy bits 0..9, PPN occupies 10..53
}
```

Typical flag macros you’ve probably got:

```c
#define PTE_V (1ULL<<0)
#define PTE_R (1ULL<<1)
#define PTE_W (1ULL<<2)
#define PTE_X (1ULL<<3)
#define PTE_U (1ULL<<4)
#define PTE_G (1ULL<<5)
#define PTE_A (1ULL<<6)
#define PTE_D (1ULL<<7)
// RSW are bits 8..9 if you want them
```

# 3) How to get the `ppn` from a physical address

```
ppn = phys_addr >> 12;   // 4 KiB pages
```

Then:

```c
uint64_t pte = pte_make(ppn, PTE_V|PTE_R|PTE_W|PTE_X|PTE_A|PTE_D);
```

# 4) Superpage alignment rules (important!)

When you create **leaf** PTEs *above level-0* (i.e., L1 = 2 MiB, L2 = 1 GiB), **some low PPN fields must be zero**:

* **L1 leaf (2 MiB page)** → **PPN0 must be 0** (i.e., `phys_addr` must be 2 MiB-aligned).
* **L2 leaf (1 GiB page)** → **PPN1=PPN0=0** (i.e., `phys_addr` must be 1 GiB-aligned).
* **L0 leaf (4 KiB page)** → only 4 KiB alignment.

If you violate this, the mapping is **illegal** and raises a page fault on access.

# 5) Examples

### (a) Pointer PTE (non-leaf) from L2 to an L1 table

```c
// L1 table must itself be page-aligned.
// For a pointer PTE: V=1, R=W=X=0
uint64_t l1_ppn = ((uint64_t)L1_hi_phys) >> 12;
L2[i2] = pte_make(l1_ppn, PTE_V);
```

### (b) 2 MiB leaf at L1 (typical for early kernel)

```c
uint64_t pa     = 0x0000000080200000ULL;     // 2 MiB-aligned
uint64_t ppn    = pa >> 12;                  // PPN encodes the frame
L1_hi[i1] = pte_make(ppn, PTE_V|PTE_R|PTE_W|PTE_X|PTE_A|PTE_D);
```

### (c) 4 KiB leaf at L0 (fine-grained)

```c
uint64_t l0_ppn = pa_4k_aligned >> 12;
L0[i0] = pte_make(l0_ppn, PTE_V|PTE_R|PTE_W|PTE_X|PTE_A|PTE_D);
```

# 6) A/D, U, G — quick guidance

* **A/D**: If your HW doesn’t set them, you can pre-set both to 1 (common in early bring-up) and later implement software A/D management if you want paging features like evict/aging.
* **U**: Leave clear for kernel mappings; set for user mappings you want U-mode to access (and consider `sstatus.SUM` for supervisor access to U pages).
* **G**: Set for global mappings (e.g., kernel text/data) so they survive ASID switches without TLB shootdowns.

# 7) Common pitfalls

* Setting any of R/W/X on a pointer PTE (should be 0) → makes it a leaf by mistake.
* Forgetting superpage alignment for L1/L2 leaves.
* Forgetting `PTE_V` (valid).
* Not fencing TLB after installing tables: `sfence.vma x0, x0` after writing `satp` or changing PTEs that affect current address space.

---

If you paste the exact `pte_make()` and the call sites you’re using (for pointer vs leaf), I can sanity-check your flags & alignment against the level each PTE lives at and the PA you intend to map.

You’re really close 👍 Let’s carefully separate the two concepts:

---

## 1. What a PTE holds

A **PTE** (page-table entry) has a **physical page number (PPN)**, not a virtual one.

```
PTE fields:
63........54 | 53........10 | 9..8 | 7..0
 reserved     |   PPN        | RSW  | flags (V,R,W,X,U,G,A,D)
```

* **PPN** = the **physical page frame number** (PA >> 12).
* So when you build a PTE, you are saying:
  “If a virtual page indexes to this entry, use this physical page frame as the target.”

---

## 2. How the VA is split

In Sv39, a **virtual address** is broken into:

```
|63..39 sign|38..30 VPN2|29..21 VPN1|20..12 VPN0|11..0 offset|
```

* **VPN2, VPN1, VPN0** are used *during the page-table walk* to pick the PTE at each level.
* These VPN fields are **indices into the page tables**, not stored in the PTE.

---

## 3. Relationship between VPN and PPN

* **VPN (from VA)** chooses *which PTE to follow*.
* **PPN (inside PTE)** gives the *physical page number to map to*.

So they are *symmetrical in the sense of width* (9 bits per level), but not the same fields:

* VPN bits [38:30], [29:21], [20:12] are from the **VA**.
* PPN bits [53:28], [27:19], [18:10] are from the **PA** stored in the PTE.

---

## 4. Worked example

Say we want to map:

```
VA = 0x0000_0000_8020_0000
PA = 0x0000_0000_8020_0000
```

* VA → VPN2=2, VPN1=1, VPN0=0.
* L2[2] points to L1_id.
* L1_id[1] = PTE with PPN = (0x8020_0000 >> 12) = 0x8020.
* That PTE has flags (V,R,W,X).
* On translation, hardware says:
  “VPN0=0 → offset=0 → final PA = PPN<<12 + offset = 0x8020_0000”.

---

✅ **Answer to your question**

* The VA’s VPN bits [38:30], [29:21], [20:12] are **not** the PPN in the PTE.
* They are the **indices** to *find* the PTEs.
* The **PPN** field in the PTE is what tells you the **physical page number** (PA >> 12) of the frame being mapped.

---

👉 Do you want me to draw a side-by-side **VA vs PTE bitfield diagram**, showing how VPN bits guide the walk and how PPN bits encode the PA at each level? That might make the symmetry clearer.

This is an excellent point of confusion — let’s separate the *indexes* (VPN fields from the VA) from the *targets* (PPN fields inside the PTE).

---

## 1. Page table walk: “where do we look?”

* When the CPU translates a VA, it extracts **VPN2, VPN1, VPN0** from the VA:

  ```
  VA: |63..39 sign|38..30 VPN2|29..21 VPN1|20..12 VPN0|11..0 offset|
  ```
* These VPN fields are just **indices** into the arrays of PTEs:

  * `VPN2` selects an entry in the L2 (root) table.
  * `VPN1` selects an entry in the next-level table.
  * `VPN0` selects an entry in the final table if you go down to level-0.

So **VPNs tell you *which PTE* to read.** They are not stored in the page table — they come from the virtual address being translated.

---

## 2. Page-table entry: “what does the entry say?”

Each **PTE** looks like this:

```
63........54 | 53........10 | 9..8 | 7..0
 reserved     |     PPN      | RSW  | flags (V,R,W,X,U,G,A,D)
```

* **PPN (physical page number)**: this is the *base physical frame number* of the mapping (or of the next-level page table if it’s a pointer entry).
* **Flags**: valid, read/write/exec, etc.
* If it’s a *leaf* entry (R/W/X ≠ 0), then the PPN is the physical page frame where the virtual page maps.
* If it’s a *pointer* entry (R=W=X=0, V=1), then the PPN points to the next-level page table.

So **the physical address is configured in the PPN field of the PTE.**

---

## 3. Where are the PTEs themselves stored?

* Page tables are nothing more than normal memory pages (4 KiB each), aligned to 4 KiB.
* Each table has 512 entries × 8 bytes = 4096 bytes = one page.
* So your `L2[512]`, `L1_id[512]`, `L1_hi[512]` arrays *are* the page tables, stored in RAM.
* The CPU only knows where the root table lives: you give it the **physical page number of L2** in the `satp` register.
* From there, hardware follows the chain of PTEs (using VPN bits as indexes), reading the PTEs directly from RAM.

---

## 4. Putting it together

1. Virtual address comes in → hardware extracts VPN2, VPN1, VPN0.
2. Hardware uses VPN2 to pick an entry from `L2[]`. That PTE has:

   * PPN = “where is the L1 table in physical memory?”
   * Flags = V=1, R/W/X=0 (means “pointer PTE”).
3. Hardware then goes to that L1 table (at physical address `PPN << 12`).
4. Uses VPN1 as index into it.
5. If that PTE is a leaf (R/W/X set), its **PPN field is the physical frame number** of the mapped page.
6. Final PA = `(PPN << 12) | (VPN0 << 12 if leaf at L1) | offset`.

---

✅ **Key point:**

* **VPN bits from VA** → tell you which PTE to read.
* **PPN bits in PTE** → tell you which physical frame (or which next-level table) you’re pointing at.
* **Page tables (PTEs)** are stored in **ordinary RAM pages**. They’re not magic; you just allocate 4 KiB-aligned pages and fill them with PTEs.

---

👉 Do you want me to draw a concrete example memory map (RAM view) showing exactly where `L2`, `L1_hi`, and `L1_id` sit in RAM, and how one entry in `L2` contains the PPN that points to the physical page holding `L1_hi`?

Perfect — let’s make it **concrete** with numbers and an ASCII RAM map.

---

## 1. Assume your RAM layout

For simplicity, say QEMU gave you DRAM from `0x8000_0000` up.
You reserved some pages for page tables:

```
Physical RAM (low portion)

0x8000_0000 ─────────────────────────────
            OpenSBI firmware, data, heap
0x8004_0000 ─────────────────────────────
            free DRAM for kernel

0x8020_0000 ─────────────────────────────
            kernel code/data (linked here)

0x8040_0000 ─────────────────────────────
            L2 page table (root)   [4 KiB]
0x8040_1000 ─────────────────────────────
            L1_id page table       [4 KiB]
0x8040_2000 ─────────────────────────────
            L1_hi page table       [4 KiB]
0x8040_3000 ─────────────────────────────
            free again...
```

---

## 2. The entries inside the tables

### L2 (root table at PA `0x8040_0000`)

It has 512 entries (`L2[0..511]`). Each is 64 bits.

* `L2[2] = PTE(ppn=0x8040_1000>>12, V=1, R=W=X=0)`
  → pointer to L1_id.
  (So VA in the **low half** near `0x8020_0000` uses this path.)

* `L2[0x102] = PTE(ppn=0x8040_2000>>12, V=1, R=W=X=0)`
  → pointer to L1_hi.
  (So VA in the **high half** near `0xffff_ffc0_8020_0000` uses this path.)

All other entries are 0 (invalid).

---

### L1_id (at PA `0x8040_1000`)

This is for identity mappings (VA == PA).

* `L1_id[1] = PTE(ppn=0x8020_0000>>12, V=1, R=1, W=1, X=1, A=1, D=1)`
  → leaf mapping, 2 MiB page.
  VA `0x0000_0000_8020_0000` → PA `0x0000_0000_8020_0000`.

---

### L1_hi (at PA `0x8040_2000`)

This is for higher-half kernel mappings.

* `L1_hi[1] = PTE(ppn=0x8020_0000>>12, V=1, R=1, W=1, X=1, A=1, D=1)`
  → leaf mapping, 2 MiB page.
  VA `0xffff_ffc0_8020_0000` → PA `0x0000_0000_8020_0000`.

---

## 3. How the hardware uses it

* **Start**: `satp.MODE=Sv39, PPN=0x8040_0000 >> 12` (points to L2 root).

* Translate VA `0x0000_0000_8020_0000`:

  * VPN2=2 → pick `L2[2]` → points to L1_id.
  * VPN1=1 → pick `L1_id[1]` → leaf with PPN=`0x8020_0000>>12`.
  * Result = PA `0x8020_0000`.

* Translate VA `0xffff_ffc0_8020_0000`:

  * VPN2=0x102 → pick `L2[0x102]` → points to L1_hi.
  * VPN1=1 → pick `L1_hi[1]` → leaf with same PPN.
  * Result = PA `0x8020_0000`.

---

## 4. ASCII memory map of the page tables

```
Physical RAM @ 0x8040_0000:

0x8040_0000  L2 root table
   [2]   = PPN=0x8040_1000>>12, V=1,R=W=X=0 → pointer to L1_id
   [0x102]= PPN=0x8040_2000>>12, V=1,R=W=X=0 → pointer to L1_hi

0x8040_1000  L1_id table
   [1]   = PPN=0x8020_0000>>12, V=1,R=1,W=1,X=1,A=1,D=1 → leaf (2 MiB)

0x8040_2000  L1_hi table
   [1]   = PPN=0x8020_0000>>12, V=1,R=1,W=1,X=1,A=1,D=1 → leaf (2 MiB)
```

---

✅ **Answer to your “where is the physical address stored?”**

* It’s in the **PPN field of the leaf PTE** (here: entries of `L1_id` and `L1_hi`).
* And the PTEs themselves live in **ordinary RAM pages** that you aligned and reserved (`L2`, `L1_id`, `L1_hi`).

---

👉 Do you want me to also show the **bit fields of one actual PTE value** (e.g. the exact 64-bit hex value stored in `L1_hi[1]` for `0x8020_0000`)? That way you can see the PPN shifted into bits [53:10] and the flags in [9:0].


Alright, let’s build one actual **64-bit PTE value** in full detail.
We’ll take your case: mapping **VA = 0xffff_ffc0_8020_0000** to **PA = 0x0000_0000_8020_0000** using a **2 MiB leaf at L1**.

---

## 1. Compute the PPN

Physical base = `0x0000_0000_8020_0000`

Shift right by 12 (because 4 KiB pages):

```
PPN = 0x0000_0000_8020_0000 >> 12
    = 0x0000_0000_0008_0200
```

Binary fields (Sv39 splits PPN across 3 parts):

* **PPN2 (bits 53..28)** = 0x0002
* **PPN1 (bits 27..19)** = 0x001
* **PPN0 (bits 18..10)** = 0x000
  (all consistent with a 2 MiB alignment, since PPN0=0)

---

## 2. Choose flags

Let’s make it readable/writeable/executable, global, accessed+dirty:

```
V=1, R=1, W=1, X=1, U=0, G=1, A=1, D=1
```

That’s bits:

```
…  7 6 5 4 3 2 1 0
    D A G U X W R V
    1 1 1 0 1 1 1 1
```

= binary `1110_1111` = hex **0xEF**

---

## 3. Pack into the PTE

General form:

```
pte = (PPN << 10) | flags
```

So:

```
pte = (0x0000000000080200 << 10) | 0xEF
    = 0x0000000002008000 | 0xEF
    = 0x00000000020080EF
```

---

## 4. Bit-field breakdown of this PTE

```
63..........................54 : 0000000000  (reserved)
53........28 (PPN2)           : 0x0002
27........19 (PPN1)           : 0x001
18........10 (PPN0)           : 0x000
 9........ 8 (RSW)            : 00
 7........ 0 (flags)          : 1110_1111
                                D=1, A=1, G=1, U=0,
                                X=1, W=1, R=1, V=1
```

---

✅ **So one actual 64-bit PTE value is:**

```
0x00000000020080EF
```

This entry says:

* **Valid leaf** at L1 (since R/W/X ≠ 0).
* Physical base = `0x8020_0000`.
* Size = 2 MiB page (because L1 leaf).
* Access: read, write, execute.
* Global, accessed, dirty.

---

👉 Would you like me to also show the **identity-map version** (`L1_id[1]`) for the *same physical address*, so you can compare side-by-side how both the high-half and identity PTEs look?

Perfect—let’s build the **identity-map** PTE for the same physical frame `PA = 0x0000_0000_8020_0000`, using a **2 MiB leaf at L1** (i.e., in `L1_id[1]`). Spoiler: the 64-bit PTE value is **identical** to the higher-half one, because a PTE encodes the **physical** target + flags, not the virtual address that chose it.

# 1) Physical frame → PPN

```
PA base          = 0x0000_0000_8020_0000
PPN (PA>>12)     = 0x0000_0000_0008_0200
PPN2 (53..28)    = 0x0002
PPN1 (27..19)    = 0x0001
PPN0 (18..10)    = 0x0000   ← 2 MiB aligned (required for L1 leaf)
```

# 2) Flags (example)

Same as before:

```
V=1, R=1, W=1, X=1, U=0, G=1, A=1, D=1  → flags = 0xEF
```

# 3) Pack the PTE

```
pte = (PPN << 10) | flags
    = (0x0000000000080200 << 10) | 0xEF
    = 0x0000000002008000 | 0xEF
    = 0x00000000020080EF
```

# 4) Where it lives and how it’s selected

* Place this value in **`L1_id[1]`** (the identity L1 table).
* Ensure **`L2[2]`** is a **pointer PTE** to the physical page containing `L1_id` (since `VPN2=2` for `VA=0x0000_0000_8020_0000`).
* Then translating any VA in the 2 MiB window:

  ```
  VA in [0x0000_0000_8020_0000, 0x0000_0000_803F_FFFF]
    VPN2=2  → L2[2] → L1_id
    VPN1=1  → L1_id[1] (leaf) → PPN=0x...80200
    PA = (PPN<<12) | (VA & 0x1F_FFFF)  → identity
  ```

# 5) Side-by-side with the higher-half entry

* **Identity map** uses: `L2[2] → L1_id[1] = 0x00000000020080EF`
* **Higher-half map** uses: `L2[0x102] → L1_hi[1] = 0x00000000020080EF`

Same **64-bit PTE** value in both leaves; only the **L2 index** (and thus which L1 table) differs due to the **virtual** address.

---

### Optional tweaks

* If this 2 MiB chunk is code, keep `X=1`; for data-only, clear `X`:

  * data-only leaf flags → `V|R|W|A|D|G` = `0b1100_1111` = **0xCF**
  * PTE would be `0x00000000020080CF`
* If you don’t want it global across ASIDs, clear `G`.

If you’d like, I can also show the exact **pointer PTE** values for `L2[2]` and `L2[0x102]` given the physical addresses of your `L1_id` and `L1_hi` tables (e.g., `0x80401000` / `0x80402000`).


heck yes—great exercise. Below is a drop-in `map_4k_region(...)` that mirrors your `map_2m_region(...)`, but builds **L0** tables so you get **4 KiB** granularity. I also included a tiny L0-table pool/allocator and the helper logic to wire pointer vs leaf PTEs correctly.

You can paste this straight into your `mmu.c` next to your existing Sv39 helpers.

```c
// --- Sv39 helpers & PTE flags (same style you already use) ---
#include <stdint.h>
#include <stddef.h>

#define PGSIZE   4096UL
#define PTE_V    (1UL<<0)
#define PTE_R    (1UL<<1)
#define PTE_W    (1UL<<2)
#define PTE_X    (1UL<<3)
#define PTE_U    (1UL<<4)
#define PTE_G    (1UL<<5)
#define PTE_A    (1UL<<6)
#define PTE_D    (1UL<<7)

static inline int vpn0(uint64_t va){ return (va >> 12) & 0x1ff; }
static inline int vpn1(uint64_t va){ return (va >> 21) & 0x1ff; }
static inline int vpn2(uint64_t va){ return (va >> 30) & 0x1ff; }
static inline uint64_t pte_make(uint64_t ppn, uint64_t flags){ return (ppn << 10) | flags; }

static inline int pte_is_valid(uint64_t p){ return (p & PTE_V) != 0; }
static inline int pte_is_leaf(uint64_t p){ return (p & (PTE_R|PTE_W|PTE_X)) != 0; }
static inline uint64_t pte_ppn(uint64_t p){ return (p >> 10) & ((1ULL<<44)-1); } // bits 53..10

// --- Your existing statically reserved tables (examples) ---
static uint64_t __attribute__((aligned(PGSIZE))) L2[512];     // root
static uint64_t __attribute__((aligned(PGSIZE))) L1_hi[512];  // for higher-half
static uint64_t __attribute__((aligned(PGSIZE))) L1_id[512];  // for identity

// --- A tiny pool of L0 tables so we can map 4 KiB pages ---
#ifndef L0_POOL_CAP
#define L0_POOL_CAP  64   // up to 64 L0 tables (64 * 2MiB = 128 MiB of 4KiB coverage)
#endif

static uint64_t __attribute__((aligned(PGSIZE))) L0_pool[L0_POOL_CAP][512];
static size_t   L0_next = 0;

static uint64_t* alloc_l0_table(void){
    if (L0_next >= L0_POOL_CAP) return NULL;
    uint64_t *t = L0_pool[L0_next++];
    // clear table
    for (int i=0;i<512;i++) t[i] = 0;
    return t;
}

// Ensure the L2->L1 pointer PTE is present (V=1, R=W=X=0)
static void ensure_l2_points_to_l1(uint64_t L2tbl[512], uint64_t L1tbl[512], uint64_t va_anchor){
    int i2 = vpn2(va_anchor);
    if (!pte_is_valid(L2tbl[i2])) {
        uint64_t l1_ppn = ((uint64_t)L1tbl) >> 12;
        L2tbl[i2] = pte_make(l1_ppn, PTE_V); // pointer PTE
    } else {
        // sanity: must be pointer, not leaf
        if (pte_is_leaf(L2tbl[i2])) {
            // In a tiny demo we won't error out hard; in production, assert/panic.
            // For now we just overwrite (but better to handle properly in your code).
            uint64_t l1_ppn = ((uint64_t)L1tbl) >> 12;
            L2tbl[i2] = pte_make(l1_ppn, PTE_V);
        }
    }
}

// Ensure L1[i1] is a pointer to an L0 table; allocate if needed.
static uint64_t* ensure_l1_points_to_l0(uint64_t L1tbl[512], int i1){
    uint64_t pte = L1tbl[i1];
    if (!pte_is_valid(pte)) {
        uint64_t *L0tbl = alloc_l0_table();
        // Handle pool exhaustion gracefully in your code; here we just return NULL.
        if (!L0tbl) return NULL;
        uint64_t l0_ppn = ((uint64_t)L0tbl) >> 12;
        L1tbl[i1] = pte_make(l0_ppn, PTE_V); // pointer PTE (R=W=X=0)
        return L0tbl;
    }
    // If valid, it must be a pointer (non-leaf) to an existing L0 table
    if (pte_is_leaf(pte)) {
        // conflicting mapping type; in real kernel, handle error properly
        return NULL;
    }
    uint64_t l0_pa = pte_ppn(pte) << 12;
    return (uint64_t*)l0_pa;
}

/**
 * map_2m_region:
 *   (already in your code) maps VA range with 2MiB leaves at L1.
 *   Signature kept here for reference so you can compare with 4KiB version.
 */
void map_2m_region(uint64_t L2tbl[512], uint64_t L1tbl[512],
                   uint64_t va_base, uint64_t pa_base,
                   uint64_t length, uint64_t flags);

/**
 * map_4k_region:
 *   Map [va_base, va_base+length) to [pa_base, pa_base+length) with **4 KiB** pages.
 *   - Uses/creates L0 tables under the provided L1tbl
 *   - Ensures the L2 entry points to the given L1tbl
 *   - flags: same PTE flags you use for 2MiB leaves (R/W/X/A/D/G/U/V)
 */
void map_4k_region(uint64_t L2tbl[512], uint64_t L1tbl[512],
                   uint64_t va_base, uint64_t pa_base,
                   uint64_t length, uint64_t flags)
{
    // Make sure L2 points at this L1 (pointer PTE)
    ensure_l2_points_to_l1(L2tbl, L1tbl, va_base);

    // Work in 4 KiB chunks
    uint64_t va   = va_base & ~(PGSIZE-1);
    uint64_t pa   = pa_base & ~(PGSIZE-1);
    uint64_t end  = (va_base + length + (PGSIZE-1)) & ~(PGSIZE-1);

    while (va < end) {
        int i1 = vpn1(va);
        int i0 = vpn0(va);

        // Ensure L1[i1] points to an L0 table
        uint64_t *L0tbl = ensure_l1_points_to_l0(L1tbl, i1);
        if (!L0tbl) {
            // In production, return an error; here we just stop mapping
            break;
        }

        // Install leaf at L0 (4 KiB page)
        uint64_t ppn = pa >> 12;
        L0tbl[i0] = pte_make(ppn, flags | PTE_V | PTE_A | PTE_D);

        va += PGSIZE;
        pa += PGSIZE;
    }
}
```

## How to compare 2 MiB vs 4 KiB mapping

* **2 MiB superpages (L1 leaf)**: fewer PTEs, faster TLB behavior, but coarse-grained.

  ```c
  // e.g., map 16 MiB from 0xFFFF_FFC0_8020_0000 → 0x0000_0000_8020_0000
  map_2m_region(L2, L1_hi, 0xFFFF_FFC080200000ULL, 0x0000000080200000ULL,
                16UL<<20, PTE_R|PTE_W|PTE_X|PTE_G);
  ```

* **4 KiB pages (L0 leaf)**: fine-grained protection (e.g., make code RX, data RW, guard pages, etc.)

  ```c
  // Same region, now with 4KiB pages:
  map_4k_region(L2, L1_hi, 0xFFFF_FFC080200000ULL, 0x0000000080200000ULL,
                16UL<<20, PTE_R|PTE_W|PTE_X|PTE_G);
  ```

* **Identity vs higher-half**: swap `L1_hi` for `L1_id`:

  ```c
  // Identity map 1 MiB around the kernel
  map_4k_region(L2, L1_id, 0x0000000080200000ULL, 0x0000000080200000ULL,
                1UL<<20, PTE_R|PTE_W|PTE_X|PTE_G);
  ```

## Notes / gotchas

* L0 tables are allocated per **VPN1 slot** you touch. The tiny pool above can cover up to `L0_POOL_CAP * 2 MiB` of 4 KiB mappings at once. Bump `L0_POOL_CAP` as needed.
* Pointer vs leaf:

  * **Pointer PTE**: `V=1`, `R=W=X=0` (points to next-level table).
  * **Leaf PTE**: `V=1` and any of `R/W/X` set (maps a page).
* After building tables and writing `satp`, remember to **`sfence.vma x0,x0`**.
* For L1/L2 leaf superpages, mind the alignment rules; the 4 KiB function only needs the usual 4 KiB alignment.

If you paste your existing `map_2m_region(...)` right below this one, you can flip a single call site to A/B test behavior (breakpoints in GDB on `ensure_l1_points_to_l0` and watch PTEs fill in).





