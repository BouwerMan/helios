% VFS_DENTRY_REFS(9) Helios Kernel API | Helios Kernel Manual

# NAME

vfs_dentry_refs - dentry lifetime and reference counting policy

# SYNOPSIS

dget(struct dentry *d);
dput(struct dentry*d);
struct dentry *dentry_alloc(...);
void dentry_add(struct dentry*d);

# DESCRIPTION

A dentry’s lifetime is governed by a reference count. Ownership means
“you hold a ref.” Passing ownership means “you transfer a ref.” The
rules below define when to take, drop, or avoid extra references.

This policy applies to all VFS path-walk and lookup helpers. Functions
prefixed with `__` are **internal**: they assume preconditions such as
normalized paths and may skip validation.

# OWNERSHIP RULES

1. **Handing off:** If you hand a dentry to a new owner (return from a
   lookup helper, store in a long-lived struct, return `/` from lookup),
   you must hold a reference **for** them.

2. **Returning cached:** If you return an existing cached dentry (hash hit),
   acquire a ref with `dget()` **before** you return it.

3. **Returning fresh:** If you are returning the child you just created via
   `dentry_alloc()`, **do not** add another ref; it already starts at `ref=1`.

4. **Walking:** Walkers own exactly one ref to the “current” component:
 cur = dget(start)
 for each step:
 next = dentry_lookup(cur, ...)
 dput(cur)
 cur = next
 return cur # with one live reference

5. **Balance all paths:** On **every** success and **every** error path,
balance your refs. If lookup succeeds but a later step fails (alloc/open/
install_fd), `dput()` before returning.

6. **Cache insertion:** `dentry_add()` takes its own cache ref. The caller
still owns exactly one ref and must `dput()` when done.

7. **Last put:** Dropping the last ref deallocates the dentry and `iput()`s
the inode. Do not touch a dentry after `dput()`.

# INTERNAL-ONLY HELPERS (`__*`)

Helpers named with a double underscore (e.g., `__foo`) are not general entry
points. They assume normalized paths (no `.`/`..`, no duplicate slashes, no
trailing slash unless root), valid pointers, and appropriate locks/context.
Callers must satisfy those preconditions or use a public wrapper.

# ERROR PATTERNS (DON’TS)

- Returning a borrowed cache pointer without `dget()`.
- Double-`dget()` on a freshly allocated child.
- Forgetting to `dput()` on early-return error paths.
- Touching a dentry after its final `dput()` (use-after-free).

# SEE ALSO

vfs_lookup(9), vfs_readdir(9)
