<!-- markdownlint-configure-file { "MD013": { "line_length": 120} } -->
# Virtual Memory Management

## Caching

Each memory page in x86_64 systems can be configured with specific cache policies using
the PWT, PCD, and PAT bits in the page table entries (PTEs).
These policies control how the CPU caches reads and writes to that page.
This is especially critical for handling memory-mapped I/O (MMIO), framebuffers, or DMA.

Below is a detailed overview of the cache types supported on modern systems when using the PAT (Page Attribute Table) mechanism.

| Cache Type                  | Description                                                                                                                                                                                                                                                                                                                                                                   |
| --------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Write-Back (WB)**         | This is the default and most efficient caching mode for general-purpose RAM. In this mode, reads and writes are cached in both the L1/L2/L3 CPU caches and may be deferred or coalesced by the CPU for performance. Writes to memory are **buffered** and written **later**. This minimizes memory bus usage. Use this for normal heap, stack, code, and data sections.       |
| **Write-Through (WT)**      | In this mode, **reads are cached**, but **writes always go through to main memory as well as being stored in cache**. This provides more predictable behavior when memory must be shared between devices (e.g., CPU and DMA controller), but it's **slower for write-heavy workloads** than WB. Use this only when you want read caching but cannot tolerate deferred writes. |
| **Write-Combining (WC)**    | This mode allows **writes to be combined in write buffers** before being flushed to memory. Reads are either uncached or weakly ordered. This is **excellent for write-heavy regions like framebuffers**, where sequential writes can be merged and written efficiently. **Avoid using this for memory you need to read from or synchronize frequently.**                     |
| **Uncacheable (UC)**        | This mode **completely disables caching**. Reads and writes go directly to memory or the device. This is required for **memory-mapped device registers (MMIO)** where caching could result in stale reads or incorrect behavior. All memory accesses are serialized. This is the safest choice for device interaction, but also the **slowest**.                              |
| **Uncacheable Minus (UC−)** | Similar to UC, but **allows some CPU-side optimizations**, such as reordering or merging of adjacent accesses under certain circumstances. This mode is only subtly different from UC and may be used when interacting with device memory that tolerates more relaxed ordering. It is sometimes faster than UC, but less predictable.                                         |
| **Write-Protected (WP)**    | Memory marked as write-protected can be **read but not written** from userspace or kernel, even in supervisor mode, **regardless of the `RW` bit**. Any write will trigger a page fault. This mode is useful for **debugging**, memory protection schemes, or guarding memory against accidental writes. It is rarely used in normal operation.                               |

| PAT | PCD | PWT | Resulting Memory Type |
| --- | --- | --- | --------------------- |
| 0   | 0   | 0   | Write-Back (WB)       |
| 0   | 0   | 1   | Write-Through (WT)    |
| 0   | 1   | 0   | Uncacheable (UC-)     |
| 0   | 1   | 1   | Uncacheable (UC)      |
| 1   | 0   | 0   | Write-Protected (WP)  |
| 1   | 0   | 1   | Write-Combining (WC)  |
| 1   | 1   | 0   | Undefined or reserved |
| 1   | 1   | 1   | Undefined or reserved |
