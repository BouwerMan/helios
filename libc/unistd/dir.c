#include "arch/syscall.h"
#include "dirent.h"
#include "errno.h"
#include "stdlib.h"
#include "sys/types.h"
#include <helios/dirent.h>
#include <stdio.h>

ssize_t __getdents(int fd, struct dirent* dirp, size_t count)
{
	// return (ssize_t)__syscall3(SYS_GETDENTS, fd, (long)dirp, (long)count);
	ssize_t res = (ssize_t)__syscall3(
		SYS_GETDENTS, (long)fd, (long)dirp, (long)count);
	if (res < 0) {
		errno = (int)-res;
	}

	return res;
}

struct dirent* readdir(DIR* dirp)
{
	if (dirp->buf_pos >= (size_t)dirp->buf_valid) {
		// Need to read more data
		dirp->buf_valid = __getdents(
			dirp->fd, (struct dirent*)dirp->buffer, dirp->buf_size);
		if (dirp->buf_valid <= 0) {
			// Error or end of directory
			if (dirp->buf_valid < 0) {
				dirp->error = (int)-dirp->buf_valid;
			}
			return nullptr;
		}
		dirp->buf_pos = 0;
	}

	dirp->entry = *(struct dirent*)(dirp->buffer + dirp->buf_pos);

	dirp->buf_pos += sizeof(struct dirent);

	return &dirp->entry;
}

DIR* opendir(const char* name)
{
	// Allocating first so I don't have to figure out how to clean up on error
	DIR* dir = zalloc(sizeof(DIR));
	if (!dir) {
		return nullptr;
	}

	dir->buf_size = 4096;
	dir->buffer = zalloc(dir->buf_size);
	if (!dir->buffer) {
		free(dir);
		errno = ENOMEM;
		return nullptr;
	}

	// TODO: Better flags
	int fd = (int)__syscall2(SYS_OPEN, (long)name, 0);
	if (fd < 0) {
		free(dir->buffer);
		free(dir);
		errno = (int)-fd;
		return nullptr;
	}

	dir->fd = fd;

	return dir;
}

void closedir(DIR* dirp)
{
	if (!dirp) {
		return;
	}

	__syscall1(SYS_CLOSE, dirp->fd);
	if (dirp->buffer) free(dirp->buffer);
	free(dirp);
}
