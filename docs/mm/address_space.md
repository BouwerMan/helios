<!-- markdownlint-configure-file { "MD013": { "line_length": 120} } -->
# Address Spaces and Memory Regions

This document covers the machine-independent virtual memory layer: `struct address_space`,
`struct memory_region` (VMAs), and how they drive the architecture-specific VMM. For the
physical side see [physical_memory.md](physical_memory.md) and [bootmem.md](bootmem.md);
for PTE cache-policy bits see [virtual_memory.md](virtual_memory.md).

Relevant sources:

* `helios/include/mm/address_space.h` — struct definitions and API
* `helios/mm/address_space.c` — region bookkeeping (metadata only)
* `helios/arch/x86_64/mmu/vmm.c` — page tables, faults, demand paging, CoW
* `helios/arch/x86_64/include/arch/mmu/vmm.h` — VMM public API and PTE flags

## The big picture

The design splits virtual memory into two layers with a deliberate division of labor:

```text
        exec / fork / mmap / page fault
                     │
   ┌─────────────────▼──────────────────┐
   │  mm/address_space.c   (generic)    │   "What SHOULD be mapped"
   │  struct address_space              │   region metadata, permissions,
   │    └── list of memory_region (VMA) │   file/anon backing policy
   └─────────────────┬──────────────────┘
                     │ vmm_* calls
   ┌─────────────────▼──────────────────┐
   │  arch/x86_64/mmu/vmm.c  (arch)     │   "What IS mapped"
   │  PML4 → PDPT → PD → PT walks,      │   PTE install/remove, TLB
   │  page faults, demand paging, CoW   │   invalidation, table pruning
   └────────────────────────────────────┘
```

`mm/address_space.c` never touches a page table; it only records intent. The VMM consults
that intent (via `get_region()` / `check_access()`) when a fault arrives and materializes
pages lazily. Almost nothing is mapped eagerly — regions describe what a fault handler is
*allowed* to fault in.

## struct address_space

One per task (`task->vas`), representing a whole virtual address space:

| Field       | Purpose                                                                  |
| ----------- | ------------------------------------------------------------------------ |
| `pml4_phys` | Physical address of the top-level page table; what gets loaded into CR3. |
| `pml4`      | Kernel-virtual (HHDM) pointer to the same table. Must stay the second field — `switch.asm` reads it by offset. |
| `vma_lock`  | rwsem protecting `mr_list` (the region metadata).                         |
| `pgt_lock`  | Spinlock protecting page-table modifications under `pml4`.               |
| `mr_list`   | Doubly linked list of `memory_region`s.                                   |

Invariants: regions in `mr_list` never overlap, and every region's `start`/`end` are
page-aligned.

The struct itself is allocated with `alloc_address_space()` (kzalloc), but the page-table
root comes separately from `vmm_create_address_space()`, which allocates a fresh PML4 and
seeds it with a copy of the kernel template (`kernel.pml4`) so the kernel half of the
address space is present in every task. `vas_set_pml4()` ties the two together and derives
`pml4_phys`. Both `exec` and `fork` follow this exact sequence.

## struct memory_region (the VMA)

One per contiguous mapping, allocated from a dedicated slab cache
(`address_space_init()` creates it at boot):

| Field        | Purpose                                                              |
| ------------ | -------------------------------------------------------------------- |
| `start, end` | Page-aligned `[start, end)` virtual range.                           |
| `prot`       | `PROT_READ/WRITE/EXEC` — the *policy* the fault handler enforces.    |
| `flags`      | `MAP_PRIVATE/SHARED/ANONYMOUS` plus VM bits.                          |
| `kind`       | Backing type: `MR_ANON`, `MR_FILE`, or `MR_DEVICE` (future/MMIO).    |
| `is_private` | Cached `MAP_PRIVATE` bit → this region is copy-on-write on write.    |
| `file/anon`  | Union of backing-specific bookkeeping (see below).                   |
| `owner`      | Back-pointer to the owning `address_space`.                          |
| `list`       | Link in `owner->mr_list`.                                             |

### struct mr_file — file-backed regions

Demand paging needs to know how virtual offsets translate to file offsets, and where the
initialized bytes end (an ELF segment's `p_filesz` is usually smaller than `p_memsz`):

* `inode` — the backing `vfs_inode`; pages come from its `inode_mapping` page cache.
* `file_lo` — page-aligned file offset corresponding to `start`.
* `file_hi` — exclusive end of initialized bytes for this segment (`p_offset + p_filesz`).
  The pager never reads past this; anything beyond it within the page is zeroed.
* `pgoff` — `file_lo >> PAGE_SHIFT`, the page-cache index of the first page.
* `delta` — intra-page bias (`p_vaddr % PAGE_SIZE`), kept mostly for debugging.

### struct mr_anon — anonymous regions

Just a `tag` for accounting/debugging (e.g. distinguishing heap from bss). Pages are
zero-filled on first touch; there is no backing store.

### ELF segments become region pairs

`exec.c` maps each `PT_LOAD` segment with `map_region()`:

* A `MR_FILE` region covers `[vstart, vstart + align_up(delta + p_filesz))` — the bytes
  that come from the file.
* If `p_memsz > p_filesz`, a second `MR_ANON` region covers the remainder (bss), which is
  pure zero-fill.

The stack and heap are additional `MR_ANON` regions. `mmap(MAP_ANONYMOUS)` creates the
same thing at runtime (`mm/mmap.c`; file-backed mmap is not implemented yet).

## Region API (mm/address_space.c)

| Function                  | Role                                                                        |
| ------------------------- | --------------------------------------------------------------------------- |
| `map_region()`            | Validate + allocate + insert a region. **Metadata only** — no PTEs created. |
| `map_device_region()`     | Like `map_region()`, but for `MR_DEVICE`: also eagerly maps PTEs via `vmm_map_device_region()`, since device regions are never demand-paged. Rejects `MAP_PRIVATE`. |
| `unmap_region()`          | The inverse, and the one region call that *does* reach the VMM: unlinks the region first, then calls `vmm_unmap_region()` to clear its PTEs, then frees the descriptor — unlinking first keeps a racing fault from repopulating a page mid-teardown. |
| `get_region()`            | Find the VMA covering an address (linear list walk).                        |
| `check_access()`          | Verify a VMA covers the address and permits the access (`-EFAULT`/`-EACCES`). |
| `add_region()` / `remove_region()` | Raw list insert/unlink; callers hold `vma_lock` for writing.      |
| `address_space_dup()`     | Fork helper: clone every region descriptor into the child, then call `vmm_fork_region()` per region to mirror present PTEs. |
| `address_space_destroy()` | `unmap_region()` every region; does not free the `address_space` itself.    |

`map_region()` enforces the invariants: page-aligned bounds, exactly one of
`MAP_PRIVATE`/`MAP_SHARED`, and for file regions a valid inode with page-aligned
`file_lo <= file_hi`.

## The VMM interface

The arch layer exposes two tiers in `arch/mmu/vmm.h`:

**Page-granular primitives** (operate on a raw `pgd_t* pml4`, no VMA knowledge):

* `vmm_map_page()` — install a PRESENT PTE; fails with `-EFAULT` if one already exists.
  Takes one mapping reference (`get_page()`) on the frame.
* `vmm_map_frame_alias()` — same, but *non-owning*: no refcount change. Used for HHDM,
  identity maps, and MMIO.
* `vmm_unmap_page()` — idempotently clear a PTE, drop the mapping reference
  (`put_page()`), prune now-empty intermediate tables, `invlpg` the address.
* `vmm_protect_page()` — swap PTE permission bits in place (used to arm/disarm CoW).
* `get_phys_addr()` — non-allocating VA→PA translation, 0 if not present.

**Region-aware operations** (take an `address_space` + `memory_region`, and — with one
exception — do their own locking):

* `vmm_install_page()` — the choke point for mapping a prepared page into a VMA. Checks
  the address is inside the region, derives PTE flags from `mr->prot`
  (`flags_from_mr()`: no `PROT_EXEC` → NX bit; private file mappings get `PAGE_WRITE`
  stripped to arm CoW), and tolerates the race where another thread mapped the same frame
  first (same frame → success, different frame → `-EEXIST`).
* `vmm_populate_one()` — demand-page a single address: no-op if already mapped, else look
  up the covering VMA and delegate to the anon or file populate helper.
* `vmm_fork_region()` — mirror one region's *present* pages into a child address space.
  For private regions it clears `PAGE_WRITE` in both parent and child, arming CoW on both
  sides. Non-present pages are simply skipped — they'll be demand-paged in whichever
  address space touches them first. **The exception**: unlike its siblings, this one does
  *not* take `vma_lock` itself. It has exactly one caller, `address_space_dup()`, which
  needs the lock held across its whole `mr_list` walk (not just one call) — so the caller
  holds `src->vma_lock` (read) for the duration and `vmm_fork_region()` documents that as
  a precondition instead of re-acquiring it per call, which would be a recursive read
  acquisition on the same rwsem.
* `vmm_unmap_region()` / `vmm_map_anon_region()` — loop the page primitives over
  `[mr->start, mr->end)`.
* `vmm_write_region()` — kernel-side byte writes into a user VAS (used by exec to place
  argv/envp); populates missing pages on demand via `vmm_populate_one()`.

### Page refcount convention

Frames carry two kinds of references. A *build ref* is what an allocator or
`imap_lookup_or_create()` returns; the code path that is constructing a mapping owns it
temporarily. A *mapping pin* is taken by `vmm_map_page()` itself on success. So the
standard populate pattern is:

```text
page = alloc_zeroed_page(...)        // build ref
vmm_install_page(vas, mr, va, page)  // takes its own mapping pin on success
put_page(page)                       // always drop the build ref;
                                     // frees the page iff install failed
```

`vmm_unmap_page()` drops the mapping pin, so a page's frame is freed once the last
mapping and the page cache are done with it.

## Fault handling walkthrough

`page_fault()` in `vmm.c` decodes the error code and splits into two paths:

**Not-present fault → demand paging.** `do_demand_paging()` runs `check_access()` against
the VMA policy, then `vmm_populate_one()`:

* `MR_ANON`: allocate a zeroed page, install it.
* `MR_FILE`: compute the file geometry —

  ```text
  page_off  = va - mr->start
  file_off  = file_lo + page_off
  to_read   = clamp(file_hi - file_off, 0, PAGE_SIZE)
  ```

  then get the page from the inode's page cache (`imap_lookup_or_create()`), fill it via
  `readpage` if it wasn't up to date, zero the tail past `to_read` (this is how the
  file/bss boundary page gets its zeros), and install it. A page entirely past `file_hi`
  is just zeroed without any I/O.

**Present + write fault → CoW.** The handler finds the VMA and decides:

* `MR_FILE` + private: always CoW — a private file mapping must never dirty the cache page.
* `MR_ANON` + private: CoW only if the frame is physically shared (`mapcount > 1`, i.e.
  after fork); otherwise the task is the sole owner and the PTE just gets `PAGE_WRITE`
  restored in place via `vmm_protect_page()`.
* Shared mappings: no CoW, restore write and mark the page dirty.

The CoW path allocates a fresh page, memcpys through the HHDM, and swaps the PTE with
`vmm_unmap_page()` + `vmm_map_page()`, which also moves the refcounts correctly. Anything
irrecoverable lands in `page_fault_fail()`, which dumps the VAS and panics (delivering
SIGSEGV instead is a TODO).

## Locking

Two locks per address space, with distinct jobs:

* `vma_lock` (rwsem, may sleep): guards `mr_list`. Readers: fault path, `check_access()`,
  populate/fork/unmap loops. Writers: anything adding or removing regions.
  `get_region()`, `add_region()`, `remove_region()` are unlocked primitives — "caller must
  hold vma_lock" for those means whichever entry point is mutating the list around them.
* `pgt_lock` (spinlock, IRQ-save): guards actual PTE/table edits under `pml4`. Held only
  around short non-sleeping sections — allocations and file I/O happen *outside* it,
  which is why the populate paths re-check for a racing mapping (`get_phys_addr()` /
  the `-EEXIST` logic in `vmm_install_page()`) after reacquiring.

Ordering: take `vma_lock` before `pgt_lock`. Never sleep (allocate, do I/O) while holding
`pgt_lock`.

**Where each lock is actually taken.** The generic layer (`map_region()`,
`map_device_region()`, `unmap_region()`) owns `vma_lock` for write, but only around the
`add_region()`/`remove_region()` call itself — allocation, validation, and freeing the
descriptor happen unlocked, since none of that touches `vas`. Critically, the write lock
is always released *before* calling down into any `vmm_*` function, because the
region-aware arch tier (`vmm_map_page`-adjacent: `vmm_install_page()`,
`vmm_unmap_region()`, `vmm_fork_region()`, and `vmm_map_device_region()` once it lands)
reacquires `vma_lock` itself, for read, around its own `pgt_lock` section. Holding the
write lock across such a call would self-deadlock on the non-reentrant rwsem. This is also
why `unmap_region()` unlinks the region (write lock) *before* calling
`vmm_unmap_region()` to clear its PTEs, rather than after: unlinking first means a
concurrent fault can no longer find the region via `get_region()`, so it can't
`vmm_populate_one()` a fresh page into memory this call is in the middle of tearing down.

Known gaps, as of this writing: `vmm_write_region()` is unlocked, and TLB invalidation is
local-CPU only — no shootdown IPIs yet. Note also that nothing in this kernel shares an
`address_space` between tasks yet (no `CLONE_VM`-equivalent) — so today, `vma_lock` has no
live concurrent writer to guard against for a given `vas`; it's there for correctness under
interrupts/preemption and to not paint the codebase into a corner if that changes.

## Lifecycle summary

* **exec**: `alloc_address_space()` → `vmm_create_address_space()` + `vas_set_pml4()` →
  `map_region()` per ELF segment / stack → pages arrive later via faults.
* **fork**: same VAS setup, then `address_space_dup()` clones descriptors and
  `vmm_fork_region()` shares present frames with CoW armed.
* **fault**: `page_fault()` → demand paging or CoW as above.
* **exit/exec-replace**: `address_space_destroy()` unmaps every region (dropping mapping
  pins, pruning empty tables) and frees the descriptors.

## SEE ALSO

[physical_memory.md](physical_memory.md), [slab.md](slab.md), [bootmem.md](bootmem.md),
[virtual_memory.md](virtual_memory.md)
