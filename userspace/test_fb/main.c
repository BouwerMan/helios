#include <fcntl.h>
#include <helios/fb.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/dev/fb", O_RDWR);
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

	if (info.format != FB_FMT_XRGB8888) {
		printf("Somehow not the right format");
		return 1;
	}

	void* fb = mmap(nullptr,
			info.vram_len,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			fd,
			0);
	if (!fb) {
		close(fd);
		return 1;
	}

	uint8_t* row_start = (uint8_t*)fb;

	for (uint32_t y = 0; y < info.height; y++) {
		uint32_t* row_pixels = (uint32_t*)row_start;

		for (uint32_t x = 0; x < info.width; x++) {
			uint32_t r = (x * 255) / info.width;
			uint32_t b = (y * 255) / info.height;

			// Map a combination of X and Y to Green (0-255)
			uint32_t g =
				((x + y) * 255) / (info.width + info.height);

			// Pack the channels into a single 32-bit integer (0x00RRGGBB)
			uint32_t color = (r << 16) | (g << 8) | (b << 0);

			row_pixels[x] = color;
		}

		// Advance the row pointer by 'pitch' bytes, NOT (width * 4)
		row_start += info.pitch;
	}

	close(fd);
	return 0;
}
