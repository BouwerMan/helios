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

## API reference (Doxygen)

Alongside the manual pages above, HeliOS also generates a browsable API
reference straight from source comments, using
[Doxygen](https://www.doxygen.nl/). The two are complementary, not
redundant:

* **Man pages** (this directory) are curated, narrative, and cover
  cross-cutting concerns — locking rules, error models, design
  rationale — that don't belong crammed into a source comment.
* **Doxygen** is generated, always in sync with the code, and gives
  you cross-linked browsing (search, "Files", "Data Structures",
  "Modules") straight from the `@brief`/`@param`/`@return` comments
  above each declaration.

### Generating it

```sh
make docs-doxygen   # Doxygen output only
make docs           # man pages + Doxygen
```

Output lands in `docs/build/doxygen/html/index.html`. Configuration is
in the `Doxyfile` at the repo root; run `doxygen -u Doxyfile` after a
Doxygen upgrade to pick up any new config keys.

### Writing doc comments

* Use Doxygen syntax: `@brief`, `@param`, `@return`, `@note`. Not
  kerneldoc (`func() - brief` / `@name:` / `Return:`).
* Write in [ASD-STE100](https://en.wikipedia.org/wiki/Simplified_Technical_English)
  Simplified Technical English: short sentences, active voice, one
  idea per sentence, consistent terminology.
* Keep it concise — describe what a function does, its parameters,
  and its return value. If a function genuinely needs a longer
  explanation (an algorithm, a multi-step protocol, locking rules
  spanning several functions), that belongs in a man9 page, not a
  wall of text in the comment.
* For a struct or type with several free functions that operate on
  it (common in C, since they aren't methods), tag each one with
  `@relates <type>` so they show up together on that type's page
  instead of scattered across the flat "Globals" list. See
  `spinlock_t` in `kernel/spinlock.h` for an example.

### Subsystem groups

Every file with Doxygen content is wrapped in an `@addtogroup <name>`
/ `@{ ... @}` block, matching its top-level source directory. This
gives the generated docs a "Modules" tab organized by subsystem
instead of raw file paths. The groups themselves are declared once in
`docs/doxygen_groups.dox`:

| Directory                  | Group          |
|-----------------------------|----------------|
| `helios/arch/`              | `arch_x86_64`  |
| `helios/drivers/`           | `drivers`      |
| `helios/fs/`                | `fs`           |
| `helios/kernel/` (excl. `tasks/`) | `kernel`  |
| `helios/kernel/tasks/`      | `sched` (subgroup of `kernel`) |
| `helios/lib/`                | `lib`          |
| `helios/mm/`                 | `mm`           |
| `libc/`                      | `libc`         |

When adding Doxygen comments to a new file, wrap it in the matching
`@addtogroup` block. If it's a genuinely new subsystem, add a
`@defgroup` for it in `docs/doxygen_groups.dox` first.

### Vendored code

Third-party code we didn't write — currently the `printf` (eyalroz)
and `liballoc` (blanham) implementations — is excluded from the
generated docs via `EXCLUDE` in the `Doxyfile`, and shouldn't be
annotated with our own doc comments. Their upstream sources are
already documented; ours would just drift. The adaptation/glue layers
around them (`printf_config.h`, `liballoc_hooks.c`, `kmalloc.h`) are
our code and stay documented normally.

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
