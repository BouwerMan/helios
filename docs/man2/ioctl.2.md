% IOCTL(2) HeliOS System Calls | Helios Manual

# NAME

ioctl - device-specific control operation

# SYNOPSIS

int ioctl(int fd, unsigned long request, void *arg);

# DESCRIPTION

Sends *request* to the device or file behind *fd*, with an optional
device-specific *arg*. The syscall layer does not interpret *request* or
*arg* at all — it just resolves *fd* to a `struct vfs_file` and calls
`file->fops->ioctl(file, request, arg)`. Each driver defines its own set of
request numbers (in its own uapi header, not a shared global list) and its
own shape for *arg*.

*arg* is conventionally a pointer to a request-specific struct, or an
integer cast to `void *` for simple cases, but this is a per-driver
convention, not something the kernel enforces.

# RETURN VALUE

On success, a non-negative value whose meaning is defined by the driver and
*request* (often 0). On error, -1 with errno set.

# ERRORS

EBADF
:   *fd* is not an open file descriptor.

ENOTTY
:   *fd* does not refer to a device that supports ioctls, or the driver has
    no `.ioctl` handler.

Any other errno the underlying driver's `.ioctl` handler chooses to return.

# NOTES

**Argument copying is the driver's job.** *arg* is passed through as an
opaque `void *`; the syscall layer has no way to know how many bytes it
points at or which direction they flow. Every `.ioctl` implementation that
touches *arg* must go through `copy_from_user` / `copy_to_user` itself
rather than dereferencing a userspace pointer directly.

Request numbers are plain sequential constants for now — there is no
`_IOR`/`_IOW` direction-and-size encoding like Linux. Keep each driver's
request numbers in its own uapi header.

# SEE ALSO

open(2), fb(4)
