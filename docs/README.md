# Helios Documentation Guide

## Directory layout

> Some of these subdirs may not exist yet; create them on first use.

* `man1/` — **User commands**
  CLI programs that ship in `userspace/` (e.g., `hsh`, `init`).
  **Naming:** `name.1.md`

* `man2/` — **System calls (UAPI)**
  The contract userspace sees: prototypes, buffers/ABIs, return values, `errno`.
  **Naming:** `getdents.2.md`, `open.2.md`
  **Target headers:** `helios/include/uapi/helios/*.h`

* `man3/` — **Library calls (libc API)**
  High-level wrappers in `libc/` (`readdir(3)`, `fopen(3)`).
  **Naming:** `readdir.3.md`

* `man4/` — **Special files & drivers**
  Device nodes (`/dev/*`), ioctls, driver user interfaces.
  **Naming:** `fb.4.md`

* `man5/` — **File formats & configuration**
  On-disk formats, config files, ABI structures.
  **Naming:** `limine.conf.5.md`, `tarfs.5.md`

* `man7/` — **Concepts & conventions**
  Tutorials, standards, and background that isn’t a specific API.
  **Naming:** `helios-conventions.7.md`

* `man8/` — **Admin tools**
  System administration commands (future you can add `mkfs-*` etc.).
  **Naming:** `mkfs-ramfs.8.md`

* `man9/` — **Kernel internals (KAPI)**
  In-kernel entry points used by other subsystems: semantics, locking, invariants.
  **Naming:** `vfs_readdir.9.md`, `vfs_getdents.9.md`
  **Target headers:** `helios/include/{fs,kernel,mm,...}/*.h`

The following directories were made before switching to manpage style, but still hold valuable design docs:

* `kernel/` — **Architecture & kernel design docs**
  Deep dives you’d share in a design review: e.g., `GDT.md`, interrupt model, scheduler notes.

* `mm/` — **Memory management design docs**
  Bootmem, physical allocator, slab, virtual memory. Rationale, diagrams, tricky edge cases.

* `init.md` — **Boot narrative**
  The “what happens from power-on to `kmain`” story, with links into `kernel/` and `mm/`.

---

## House style (keep it boring and consistent)

### File naming

* Manual pages: `NAME.SECTION.md` — e.g., `getdents.2.md`, `vfs_readdir.9.md`.
* Design docs: descriptive names (`virtual_memory.md`, `GDT.md`).

### Structure per manual page

Use concise, predictable sections so readers can skim. Example skeleton:

```md
% NAME(SECTION) Helios Manual

# NAME

name - one-line purpose

# SYNOPSIS

prototype(s) or invocation

# DESCRIPTION

What it does, externally observable behavior.

# RETURN VALUE

Exact meaning of success/EOF/error.

# ERRORS

List of errno or negative errors with when/why.

# NOTES

Semantics quirks, limits, compatibility.

# SEE ALSO

foo(2), bar(9), links to design docs
```

For **man9** pages, swap `ERRORS` for an “Error model” paragraph and add **LOCKING** and **INTERACTIONS** sections:

```md
# LOCKING

Which locks are held, when, and for how long. Allowed reorderings. Snapshot guarantees.

# INTERACTIONS

How it plays with lseek, dcache, scheduler, etc.
```
