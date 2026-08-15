#include <errno.h>
#include <fcntl.h>
#include <helios/fb.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
	int ok = 1;

	// ENOTTY: open something real that has no .ioctl handler.
	// "/" is guaranteed to exist and isn't a chardev.
	int fd = open("/dev/fb", O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	struct fb_screeninfo info = { 0 };
	int res = ioctl(fd, FBIOGET_SCREENINFO, &info);
	if (res < 0) {
		perror("ioctl");
		return 1;
	}

	printf("FBIOGET_SCREENINFO:\n");
	printf("    Width: %u\n", info.width);
	printf("    Height: %u\n", info.height);
	printf("    Pitch: %u\n", info.pitch);
	printf("    BPP: %u\n", info.bpp);
	printf("    Format: %s\n", __get_fb_format_name(info.format));
	printf("    Capabilities: 0x%x\n", info.caps);
	printf("    VRAM Length: %zu\n", info.vram_len);

	close(fd);

	return ok ? 0 : 1;
}
