
<!-- markdownlint-configure-file { "MD013": { "line_length": 120} } -->
# Slab Allocator

The slab allocator is our main method for allocating fixed-size kernel objects.
> Inspired by the Linux kernel’s slab cache. We’re not reinventing the wheel, just bolting it on worse.

## Theory of Operation

The slab allocator is designed to efficiently allocate memory for objects of the same size and alignment.
It sits on top of the buddy allocator and carves up page-sized slabs into aligned chunks.

The allocator maintains a set of slab caches (`struct slab_cache`), each responsible for a specific object size.
Each cache manages one or more slabs (`struct slab`), and each slab tracks free slots using a freelist stack.

The important bits:

* Object size is rounded up to meet alignment constraints.
* Each object has optional **redzones** before and after it to detect overflows and underflows.
* We optionally **poison** freed memory with a known pattern to catch use-after-free bugs.
* Objects are allocated from partially filled slabs first, then empty slabs. If none are available, we grow.
* Slabs are released back to the buddy allocator when we have more than `MAX_EMPTY_SLABS` empty slabs.
* Slabs are tracked in `empty`, `partial`, and `full` lists for efficient allocation and cleanup.

## Debugging Features

When `SLAB_DEBUG` is enabled (compile-time flag), the following features are active:

### Redzones

Each object is wrapped in 4-byte "redzone" markers (`0xDEADBEEF`) at the start and end.
These are verified when an object is freed. If they’re corrupted, we scream in the logs and mark the slab as tainted.

### Poisoning

Freed objects are filled with `0x5A` bytes.
When an object is re-allocated, we check that the poison pattern is still intact.
If it’s not, someone touched memory they shouldn’t have.

### Slab Integrity Flags

Each slab can be flagged as `debug_error = true` to indicate that it has experienced corruption.
These slabs are excluded from future allocations and available for debugging or core dumps.

## Initialization

Slab caches are set up using:

```c
int slab_cache_init(struct slab_cache* cache, const char* name, size_t object_size, size_t object_align,
                    void (*constructor)(void*), void (*destructor)(void*));
```

You provide:

* A name (for logging).
* The object size.
* The desired alignment (defaults to L1 cache line size).
* Optional constructor and destructor functions for object lifecycle management.

Each cache then uses a fixed number of pages per slab (usually 16).
A header is prepended to the slab, followed by the objects themselves.

## Layout and Alignment

Redzones complicate alignment, since we want the object itself to be aligned even though it follows a redzone.

To handle this:

1. We compute a memory region large enough to fit:
    * Head redzone
    * Aligned object
    * Tail redzone
    * Alignment padding
2. We offset the object to the first aligned address after the head redzone.
3. This guarantees that every object returned is properly object_align-aligned.

```c
obj_start = ALIGN_UP(raw_ptr + REDZONE_SIZE, object_align);
```

This way:

* obj_start is properly aligned.
* We preserve redzones on both sides.
* No weird alignment violations sneak in.

### Memory Layout Diagram

```
               +---------------------------------------------------------------+
slab base ---> |                    slab metadata (aligned)                    |
               +---------------------------------------------------------------+
               |                            ...                                |
               +---------------------------------------------------------------+
               |                        object 0                               |
               | (aligned start)                                               |
               |                                                               |
               |   [ head redzone ]     [ object memory ]     [ tail redzone ] |
               |       4 bytes             object_size             4 bytes     |
               |     (0xDEADBEEF)          (e.g., 64B)            (0xDEADBEEF) |
               |                                                               |
               |   obj_start = ALIGN_UP(raw_ptr + REDZONE_SIZE, object_align)  |
               |   redzone_start = obj_start - REDZONE_SIZE                    |
               |   redzone_end   = obj_start + object_size                     |
               +---------------------------------------------------------------+
               |                            ...                                |
               +---------------------------------------------------------------+
               |                        object N                               |
               |                                                               |
               |   [ head redzone ]     [ object memory ]     [ tail redzone ] |
               |                                                               |
               +---------------------------------------------------------------+
```

NOTES:

* slab->free_stack[i] points to obj_start
* Objects are placed with stride:
    stride = object_size + 2 * REDZONE_SIZE + object_align
* Alignment is preserved because obj_start is computed after aligning raw_ptr + redzone
* redzones wrap each object individually and are checked on free

## Usage

See [slab.h](../../helios/include/mm/slab.h) for details.

### Allocation Functions and Deallocation Functions

| Function | Description |
|---|---|
| void\* slab_alloc(struct slab_cache\* cache) | Allocates a single object from the given cache. |
| void slab_free(struct slab_cache\* cache, void\* object) | Frees a previously allocated object. |

## Future Work

* Add per-CPU slab freelists to eliminate contention.
* Add object coloring or randomized slab offsets to better detect UAF patterns.
* Possibly add tracing hooks for allocation site tracking and profiling.
