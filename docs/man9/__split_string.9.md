% __SPLIT_STRING(9) Helios Kernel API | Helios Kernel Manual
# NAME
__split_string - split a filesystem path into parent and basename

# SYNOPSIS
int __split_string(const char *path, char **parent_out, char **name_out);

# DESCRIPTION
`__split_string()` parses a canonical-ish path and returns two components:
the *parent path* and the *basename*. Trailing slashes are ignored and
adjacent slashes are treated as a single separator. The function allocates
both outputs with `kzalloc`; the caller owns and must `kfree()` them.

This is a **kernel-internal** helper (not UAPI).

# CONTRACT
- *path* must be a valid, NUL-terminated string.
- Trailing slash is ignored: `/usr/bin/` → parent=`/usr`, name=`bin`.
- Multiple slashes collapse: `//usr///bin` → `/usr/bin`.
- Root-only or all-slash inputs (e.g., `/`, `///`) are invalid.
- `"."` and `".."` are not valid basenames (`-VFS_ERR_INVAL`).
- Basename length must be `<= VFS_MAX_NAME`; longer → `-VFS_ERR_NAMETOOLONG`.
- On success, `*parent_out` and `*name_out` are `kzalloc`’d.
- On allocation failure, both outputs are set to `nullptr` and
  `-VFS_ERR_NOMEM` is returned.
- On any failure, both outputs are set to `nullptr` for predictable cleanup.

# RETURN VALUE
Returns `VFS_OK` on success.
Returns `<0` on error as a negative `-VFS_ERR_*` code.

# ERRORS
- `-VFS_ERR_INVAL` — invalid input (root-only, all-slash, `"."`, `".."`).
- `-VFS_ERR_NAMETOOLONG` — basename exceeds `VFS_MAX_NAME`.
- `-VFS_ERR_NOMEM` — allocation failed.

# EXAMPLES
| Input path     | parent_out | name_out | Return          |
|----------------|------------|----------|-----------------|
| `/usr/bin/ls`  | `/usr/bin` | `ls`     | `VFS_OK`        |
| `foo/bar/`     | `foo`      | `bar`    | `VFS_OK`        |
| `/`            | `nullptr`  | `nullptr`| `-VFS_ERR_INVAL`|
| `/..`          | `nullptr`  | `nullptr`| `-VFS_ERR_INVAL`|
| `////`         | `nullptr`  | `nullptr`| `-VFS_ERR_INVAL`|

# CONTEXT
May sleep (allocates via `kzalloc`); no locks are acquired.

# SEE ALSO
vfs_lookup(9), getdents(2)
