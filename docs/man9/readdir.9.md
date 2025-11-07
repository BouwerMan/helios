% VFS_READDIR(9) HeliOS Kernel API | Helios Kernel Manual

# NAME

vfs_readdir - VFS one-entry directory iterator

# SYNOPSIS

int vfs_readdir(struct file \*dir, struct dirent\* out, off_t off);

# CONTRACT

*off* is a global position: 0=".", 1="..", >=2 children.
On emit: return 1, set `out->d_off` to next global pos, advance `dir->f_pos`.
Return 0 on EOF; <0 on error.

# SEMANTICS

"." and ".." are synthesized locally. For off>=2: child_index=off-2;
backend `->readdir(child_index)` returns next_child; VFS sets
`out->d_off = next_child + 2` and updates `f_pos`.
Exactly one entry per call.

# LOCKING

Holds directory inode read-lock. Best-effort snapshot under mutations.

# INTERACTIONS

Compatible with `lseek` cookies. Absurdly large *off* is EOF.

# BACKEND CONTRACT

`->readdir(i)` emits exactly one entry for *i* or signals EOF; must not
return partially initialized entries.

# SEE ALSO

getdents(2), vfs_getdents(9)
