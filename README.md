# riscv64

Perfect — let’s draw how your three arrays (`L2`, `L1_id`, `L1_hi`) are actually used by the Sv39 page walker.

---

## 1. Sv39 VA breakdown

In Sv39, a **64-bit virtual address** is divided as:

```
|63 .......... 39|38 .......... 30|29 .......... 21|20 .......... 12|11 ...... 0|
    sign bits         VPN[2]            VPN[1]          VPN[0]          page offset
```

* **VPN\[2]** → index into **L2** (root) table
* **VPN\[1]** → index into **L1** table
* **VPN\[0]** → index into **L0** table (or use a superpage entry at L1/L2)
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

1. Extract VA\[38:30] → index into `L2`.
2. That entry points to `L1_id`.
3. Use VA\[29:21] to select a PTE in `L1_id`.
4. That entry is a **leaf**, giving PPN (physical page number) for 2 MiB superpage.
5. Combine PPN with VA\[20:12] + page offset.

Result: VA == PA.

---

## 5. Example: Higher-half mapping

Say you want VA `0xffff_ffc0_8020_0000` → PA `0x0000_0000_8020_0000`.

1. Extract VA\[38:30] (these high addresses are sign-extended, so top 25 bits are 1).
2. That index in `L2` points to `L1_hi`.
3. Use VA\[29:21] to select entry in `L1_hi`.
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
* VA\[38:30] selects the slot in L2. That slot either points to L1\_id or L1\_hi.
* VA\[29:21] selects the slot in the L1, which holds a leaf mapping (2 MiB pages in your demo).

---

👉 Do you want me to extend this into a **worked concrete example** with real hex addresses and show exactly which L2/L1 indices are used for `0x8020_0000` and `0xffff_ffc0_8020_0000`?

