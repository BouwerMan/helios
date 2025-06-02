<!-- markdownlint-configure-file { "MD013": { "line_length": 120} } -->
# Boot Time Memory Management

In order to setup our [super cool buddy allocator](./physical_memory.md) we need to allocate some memory for it.
So to do this we will create a memory allocator that will allocate memory during system init.

> This allocation style is based heavily on the
[Linux kernel's bootmem allocator](https://www.kernel.org/doc/gorman/html/understand/understand008.html).

## Theory of Operation

The bootmem allocator is a simple bitmap allocator.
This is mainly because I am lazy and I've used this type of allocator before in our original PMM.
(For reference, the linux kernel uses a first fit algorithm)

ALL memory allocated by the bootmem allocator is considered system critical.
This means that once the bootmem allocator is [decommissioned](Decommissioning) the memory will not be freeable.

## Initialization

During bootmem initialization we will scan the memory map and create a bitmap of the available memory.
Once this is created, we will allocate the memory for our global mem_map array.
For each page that isn't currently used, we clear the reserved bit in its `struct page`.

## Usage

The following functions are available to allocate memory before the buddy allocator is initialized:

### Allocation Functions

| Function | Description |
|---|---|
| void* bootmem_alloc_page(void) | Used to allocate a single page of memory. |
| void* bootmem_alloc_contiguous(size_t count) | Used to allocate several physically contiguous pages of memory. |

### Deallocation Functions

| Function | Description |
|---|---|
| void bootmem_free_page(void* addr) | Used to free a single page of memory. |
| void bootmem_free_contiguous(void* addr, size_t count) | Used to free several physically contiguous pages of memory. |

## Decommissioning

Once the buddy allocator is initialized, it automatically decommissions the bootmem allocator with a call to `bootmem_free_all()`.
This function gives every page that it considers free to the buddy allocator by calling `__free_page()`.
Once all the pages are owned by the buddy allocator, we free the bitmap and point the global pointer to `NULL`.
