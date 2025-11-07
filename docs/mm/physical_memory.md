<!-- markdownlint-configure-file { "MD013": { "line_length": 120} } -->
# Physical Memory Management

Our main physical memory management system is the buddy allocator.
> A lot of the operation is based on the
[Linux kernel's physical memory allocator](https://www.kernel.org/doc/gorman/html/understand/understand009.html).

## Theory of Operation

I'm not going to fully explain how a buddy allocator works.
So have fun with that future Dylan enjoy re-learning 😘.
> Okay I'm not that mean, here you go:
[Buddy memory allocation](https://en.wikipedia.org/wiki/Buddy_memory_allocation).

The important, implementation specific, details are as follows:

* We assume UMA architecture. This will need to be fixed in the future (not now lmao).
* The number of pages allocated is rounded up to the nearest power of two.
We ask `__alloc_pages_core()` to allocated 2^order pages.
* `get_free_pages()` takes care of rounding and converting from a page count to order.
  * This function is called by `get_free_page()`
* We are using a flat array model.
* There is a global `struct buddy_allocator` that contains the free lists for each order (MAX_ORDER + 1).
* The max order we allow is defined in [page_alloc.h](../../helios/include/mm/page_alloc.h) (~10).
  * This gives us a maximum of 1024 pages (4MiB) per allocation.
* While the Linux kernel has GFP_FLAGS. We don't actually do anything with the flags yet.
* I haven't implemented DMA allocations yet either.
* I am now realizing that we never use the state other than `BLOCK_FREE`, probably can just delete everything else.

## Initialization

The most important part of initialization is the decommissioning of the
[bootmem allocator](./bootmem.md).

## Usage

The following functions are available to allocate memory before the buddy allocator is initialized.
Certain functions have been left out as they are mainly used internally.
See [page_alloc.h](../../helios/include/mm/page_alloc.h) for more details.

### Allocation Functions

| Function | Description |
|---|---|
| void\* get_free_page(flags_t flags) | Used to allocate a single page of memory. |
| void\* get_free_pages(flags_t flags, size_t pages) | Used to allocate several physically contiguous pages of memory. |

### Deallocation Functions

| Function | Description |
|---|---|
| void free_page(void* addr) | Used to free a single page of memory. |
| void free_pages(void* addr, size_t pages) | Used to free several physically contiguous pages of memory. |
