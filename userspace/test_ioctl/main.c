#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
	int ok = 1;

	// ENOTTY: open something real that has no .ioctl handler.
	// "/" is guaranteed to exist and isn't a chardev.
	int fd = open("/", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	if (ioctl(fd, 0, NULL) != -1 || errno != ENOTTY) {
		printf("FAIL: expected ENOTTY, got ret=? errno=%d\n", errno);
		ok = 0;
	} else {
		printf("PASS: ioctl on non-ioctl fd -> ENOTTY\n");
	}

	close(fd);

	// EBADF: fd is now closed, so it's guaranteed invalid.
	if (ioctl(fd, 0, NULL) != -1 || errno != EBADF) {
		printf("FAIL: expected EBADF, got errno=%d\n", errno);
		ok = 0;
	} else {
		printf("PASS: ioctl on closed fd -> EBADF\n");
	}

	return ok ? 0 : 1;
}
