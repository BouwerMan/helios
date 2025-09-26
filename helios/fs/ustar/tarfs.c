#include "fs/ustar/tar.h"
#include "fs/vfs.h"
#include <kernel/helios.h>
#include <lib/log.h>

static size_t oct2bin(unsigned char* str, size_t size)
{
	size_t n = 0;
	unsigned char* c = str;
	while (size-- > 0 && *c >= '0' && *c <= '7') {
		n *= 8;
		n += *c - '0';
		c++;
	}
	return n;
}

void unpack_tarfs(void* archive_address)
{
	log_info("Unpacking initramfs from tar archive at %p", archive_address);

	unsigned char* ptr = archive_address;

	while (true) {
		struct ustar_header* header = (struct ustar_header*)ptr;

		// The end of a TAR archive is marked by two 512-byte blocks of zeros.
		// Checking the first byte of the name is a reliable way to detect this.
		if (header->name[0] == '\0') {
			break;
		}

		void* file_data = (void*)(ptr + 512);

		size_t file_size = oct2bin((unsigned char*)header->size,
					   ARRAY_SIZE(header->size));

		if (header->typeflag == '5') {
			vfs_mkdir(header->name, VFS_PERM_ALL);
		} else if (header->typeflag == '0' ||
			   header->typeflag == '\0') {
			int fd = vfs_open(header->name, O_CREAT | O_WRONLY);
			if (fd >= 0) {
				vfs_write(fd,
					  (const char*)file_data,
					  file_size);
				vfs_close(fd);
			} else {
				log_error("tarfs: Failed to create file %s",
					  header->name);
			}
		}

		// Advance the pointer to the next header.
		// The file data is padded to a 512-byte boundary.
		size_t data_size_padded = CEIL_DIV(file_size, 512UL) * 512UL;
		ptr += 512 + data_size_padded;
	}
	log_info("Initramfs unpacked into rootfs");
}
