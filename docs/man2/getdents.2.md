% GETDENTS(2) HeliOS System Calls | Helios Manual

# NAME

getdents - read directory entries

# SYNOPSIS

long getdents(int fd, struct dirent *buf, size_t len);

# DESCRIPTION

Reads packed directory entries from the open directory *fd* into *buf*.
`d_off` is a global cookie suitable for `lseek(fd, d_off, SEEK_SET)`.

# RETURN VALUE

On success, number of bytes written; 0 on EOF; -1 on error with errno set.

# ERRORS

EBADF, ENOTDIR, EFAULT, EINVAL, EOVERFLOW, EINTR.

# NOTES

Cookies: 0=".", 1="..", >=2 children. Large cookies past end act as EOF.

# SEE ALSO

readdir(3), vfs_readdir(9)
