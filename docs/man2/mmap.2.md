% MMAP(2) HeliOS System Calls | Helios Manual

# NAME

mmap, munmap - map or unmap memory

# SYNOPSIS

void \*mmap(void \*addr, size_t length, int prot, int flags, int fd, off_t offset);

int munmap(void \*addr, size_t length);

# DESCRIPTION

`mmap()` asks the kernel for a range of virtual memory in the calling process's
address space. There are two shapes of mapping, selected by `flags`:

**Anonymous** (`flags & MAP_ANONYMOUS`) — a block of zero-initialized memory not backed
by any file. *fd* and *offset* are ignored. This is what `malloc`-style allocators and
stack/heap growth use.

**File-backed** (`flags` without `MAP_ANONYMOUS`) — the mapping is backed by the open
file descriptor *fd*. `mmap_sys()` resolves *fd* to a `struct vfs_file` and, if the
underlying driver has a `.mmap` hook in its `file_ops`, calls
`file->fops->mmap(file, addr, length, prot, flags, offset)` to let the driver build the
mapping itself. There is no generic page-cache-backed file mapping — only drivers that
implement `.mmap` are mappable. See BUGS.

## Arguments

`addr`
:   A *hint* for where to put the mapping. Almost always pass `nullptr` and let the
    kernel choose — `choose_base_addr()` starts at `DEF_ADDR` (`0x555555554000`) and
    walks forward a page at a time until it finds free space. There is currently no way
    to force a specific address; the hint is accepted but nothing enforces it beyond
    that starting point.

`length`
:   Size of the mapping in bytes. Rounded up to a multiple of the page size
    (`PAGE_SIZE`) before anything else happens. Zero is rejected.

`prot`
:   Desired page protection, from `enum MMAP_PROT` (`uapi/helios/mman.h`):
    `PROT_NONE`, `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`, bitwise-OR'd. Passed straight
    through to the mapping code (`map_region()` for anonymous, the driver's `.mmap` for
    file-backed) — nothing in `mmap_sys()` itself validates combinations.

`flags`
:   Mapping type, from `enum MMAP_FLAGS` (`uapi/helios/mman.h`):

    `MAP_ANONYMOUS`
    :   Not file-backed; see above. Takes priority — if set, the file-backed path never
        runs and *fd*/*offset* are never inspected.

    `MAP_PRIVATE`
    :   Copy-on-write, changes not visible to other mappers. What individual drivers do
        with this on a file-backed mapping is up to them — e.g. the framebuffer driver
        rejects it outright, since a private copy of VRAM makes no sense.

    `MAP_SHARED`
    :   Changes visible to other mappers of the same object. The only sensible choice
        for a device mapping like `/dev/fb0`.

    `MAP_GROWSDOWN`
    :   Declared, not implemented.

`fd`
:   File descriptor to map, for the file-backed case. Pass `-1` for anonymous mappings
    (ignored either way once `MAP_ANONYMOUS` is set, but `-1` is the convention). Must be
    an open fd — resolved via `get_file(fd)`.

`offset`
:   Byte offset into whatever *fd* refers to, where the mapping should start. **Must be
    page-aligned** — `mmap_sys()` checks `offset & (PAGE_SIZE - 1)` before even looking
    up *fd* and fails the whole call if it isn't 0 mod `PAGE_SIZE`. What "offset into
    the file" means is driver-defined for device files; for `/dev/fb0` it's a byte
    offset into VRAM (`fbdev.vram_paddr + offset`).

# RETURN VALUE

On success, a pointer to the start of the mapping (page-aligned, in the region chosen by
`choose_base_addr()`). **On error, `mmap()` currently returns `nullptr`, not
`(void*)-1`/`MAP_FAILED`** — see BUGS. The libc wrapper (`libc/sys/mman.c`) does no
translation; it hands back whatever `mmap_sys()` returned. `errno` is **not** set by
anything in this path today.

`munmap()` always returns 0. It does not actually unmap anything — see BUGS.

# ERRORS

There is no errno reporting yet (see BUGS), but the failure *causes*, in the order
`mmap_sys()` checks them, are:

- `length == 0`.
- File-backed only: *offset* is not page-aligned.
- File-backed only: *fd* does not resolve to an open file (`get_file(fd)` fails).
- File-backed only, driver's `.mmap` returns negative: the driver rejected the request
  (e.g. bad *offset*/*length* range, unsupported *flags*). The specific reason is
  whatever the driver logged; it does not reach the caller.
- Anonymous only: `map_region()` failed (out of virtual space, allocation failure).

# NOTES

**Page alignment applies to `offset`, not `addr`.** The *addr* hint has no alignment
requirement enforced here, though in practice `choose_base_addr()` always returns a
page-aligned address since it starts from a page-aligned `DEF_ADDR` and steps by
`PAGE_SIZE`.

**Not every open file is mappable.** Mappability is entirely a property of whether
`file->fops->mmap` is non-null — there is no generic path through the VFS/page cache.
Check the specific driver's man4 page (e.g. `fb(4)`) for what it accepts.

# BUGS

**File-backed mmap on a driver with no `.mmap` hook panics the kernel**, rather than
returning an error. `mmap_sys()` falls through to `kunimpl("File-backed mmap")`, which is
a hard panic (or a failed `kassert`, also fatal) — not a graceful `-ENOSYS`. Only map
files/devices you know implement `.mmap`.

**Failure returns `nullptr`, and callers cannot distinguish "failed" from "mapped at
address 0."** In practice nothing ever gets mapped at address 0, so this is safe *in
practice*, but there is no `MAP_FAILED` sentinel and no errno — `mmap.h`'s doc comment
describes the POSIX contract (`MAP_FAILED` + errno) that the implementation does not
actually provide yet. Tracked as a TODO at the top of `mm/mmap.c`.

**`munmap()` is a no-op** (`return 0;`, unconditionally). Every mapping — anonymous or
device — leaks for the lifetime of the process. Harmless for a short-lived test program;
not harmless for anything long-running or repeatedly `fork`/`exec`'d.

**Device mappings (`MR_DEVICE`, from `map_device_region()`) are not inherited across
`fork()`** — `vmm_fork_region()` returns `-ENOTSUP` for that region kind. A process that
holds an `mmap()`'d device file and then calls `fork()` will fail the fork. This is
deliberate (documented in issue #07/`fb(4)`), not a bug, but it's easy to trip over.

# SEE ALSO

fork(2), fb(4)
