#include "drivers/fb.h"
#include "drivers/device.h"
#include "fs/devfs/devfs.h"
#include "fs/vfs.h"
#include "kernel/limine_requests.h"
#include "kernel/panic.h"
#include "kernel/semaphores.h"
#include "lib/log.h"
#include "lib/string.h"

#include <uapi/helios/errno.h>

struct fb_device fbdev = { 0 };

static struct file_ops fb_fops = {
	.read = nullptr,
	.write = fb_write,
	.ioctl = nullptr,
	.mmap = nullptr,
	.open = nullptr,
};

void fb_init()
{
	// Ensure we got a framebuffer.
	if (framebuffer_request.response == NULL ||
	    framebuffer_request.response->framebuffer_count < 1) {
		panic("No framebuffer found");
	}

	struct limine_framebuffer* fb =
		framebuffer_request.response->framebuffers[0];

	sem_init(&fbdev.sem, 1);

	fbdev.width = (u32)fb->width;
	fbdev.height = (u32)fb->height;
	fbdev.pitch = (u32)fb->pitch;
	fbdev.bpp = (u32)fb->bpp;
	fbdev.format = FB_FMT_XRGB8888; // TODO: support more formats

	fbdev.vram_paddr = HHDM_TO_PHYS(fb->address);
	fbdev.vram_len = fb->pitch * fb->height;

	fbdev.caps = 0;

	/*
	 * Now we init the fb character device
	 */

	dev_t base;
	int e = alloc_chrdev_region(&base, 1, "fb");
	if (e < 0) {
		log_error("Failed to allocate chrdev region for fb: %d", e);
		panic("Cannot continue without framebuffer");
	}

	fbdev.cdev.name = strdup("fb");
	if (!fbdev.cdev.name) {
		log_error("Failed to allocate fb chrdev name");
		panic("Cannot continue without framebuffer");
	}

	fbdev.cdev.base = base;
	fbdev.cdev.count = 1;
	fbdev.cdev.fops = &fb_fops;
	fbdev.cdev.drvdata = &fbdev;

	chrdev_add(&fbdev.cdev, fbdev.cdev.base, fbdev.cdev.count);

	struct vfs_superblock* devfs_sb = vfs_get_sb("/dev");
	if (!devfs_sb) {
		log_error("Failed to find devfs superblock");
		panic("Cannot continue without console");
	}

	devfs_map_name(devfs_sb,
		       fbdev.cdev.name,
		       fbdev.cdev.base,
		       FILETYPE_CHAR_DEV,
		       0666,
		       0);

	log_info("Framebuffer initialized");
}

ssize_t
fb_write(struct vfs_file* file, const char* buffer, size_t count, off_t* offset)
{
	// TODO: Get fbdev from cdev->drvdata
	(void)file;
	(void)offset;

	if (count > fbdev.vram_len) {
		count = fbdev.vram_len;
	}
	// TODO: Should probably map the vram_paddr in the process instead of
	// straight hhdm
	memcpy((void*)PHYS_TO_HHDM(fbdev.vram_paddr), buffer, count);

	return (ssize_t)count;
}

int fb_mmap(struct vfs_file* file, void* addr, size_t len, int prot, off_t off)
{
	(void)file;
	(void)addr;
	(void)len;
	(void)prot;
	(void)off;

	return -ENOSYS;
}
