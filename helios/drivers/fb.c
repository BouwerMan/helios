#include "drivers/fb.h"
#include "drivers/device.h"
#include "drivers/screen.h"
#include "drivers/term.h"
#include "fs/devfs/devfs.h"
#include "fs/vfs.h"
#include "kernel/limine_requests.h"
#include "kernel/panic.h"
#include "kernel/semaphores.h"
#include "kernel/tasks/scheduler.h"
#include "lib/log.h"
#include "lib/string.h"
#include "mm/address_space.h"
#include "mm/page.h"

#include <uapi/helios/errno.h>
#include <uapi/helios/fb.h>
#include <uapi/helios/mman.h>

struct fb_device g_fbdev = { 0 };

static struct file_ops fb_fops = {
	.read = nullptr,
	.write = fb_write,
	.ioctl = fb_ioctl,
	.mmap = fb_mmap,
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

	sem_init(&g_fbdev.sem, 1);

	g_fbdev.width = (u32)fb->width;
	g_fbdev.height = (u32)fb->height;
	g_fbdev.pitch = (u32)fb->pitch;
	g_fbdev.bpp = (u32)fb->bpp;
	g_fbdev.format = FB_FMT_XRGB8888; // TODO: support more formats

	g_fbdev.vram_paddr = HHDM_TO_PHYS(fb->address);
	g_fbdev.vram_len = fb->pitch * fb->height;

	g_fbdev.caps = FB_CAP_MMAP;

	g_fbdev.mode = FB_MODE_TEXT;

	/*
	 * Now we init the fb character device
	 */

	dev_t base;
	int e = alloc_chrdev_region(&base, 1, "fb");
	if (e < 0) {
		log_error("Failed to allocate chrdev region for fb: %d", e);
		panic("Cannot continue without framebuffer");
	}

	g_fbdev.cdev.name = strdup("fb");
	if (!g_fbdev.cdev.name) {
		log_error("Failed to allocate fb chrdev name");
		panic("Cannot continue without framebuffer");
	}

	g_fbdev.cdev.base = base;
	g_fbdev.cdev.count = 1;
	g_fbdev.cdev.fops = &fb_fops;
	g_fbdev.cdev.drvdata = &g_fbdev;

	chrdev_add(&g_fbdev.cdev, g_fbdev.cdev.base, g_fbdev.cdev.count);

	struct vfs_superblock* devfs_sb = vfs_get_sb("/dev");
	if (!devfs_sb) {
		log_error("Failed to find devfs superblock");
		panic("Cannot continue without console");
	}

	devfs_map_name(devfs_sb,
		       g_fbdev.cdev.name,
		       g_fbdev.cdev.base,
		       FILETYPE_CHAR_DEV,
		       0666,
		       0);

	log_info("Framebuffer initialized");
}

ssize_t fb_write(struct vfs_file* file,
		 const char __user* buffer,
		 size_t count,
		 off_t* offset)
{
	(void)offset;

	struct fb_device* fbdev = file->private_data;

	if (count > fbdev->vram_len) {
		count = fbdev->vram_len;
	}

	copy_from_user((void*)PHYS_TO_HHDM(fbdev->vram_paddr), buffer, count);

	return (ssize_t)count;
}

int fb_mmap(struct vfs_file* file,
	    void* addr,
	    size_t len,
	    int prot,
	    int flags,
	    off_t off)
{
	char prot_str[4] = { (prot & PROT_READ) ? 'r' : '-',
			     (prot & PROT_WRITE) ? 'w' : '-',
			     (prot & PROT_EXEC) ? 'x' : '-',
			     '\0' };
	log_debug("Mapping fb to %p + %zu, length=%zu, prot=%s, flags=0x%x",
		  addr,
		  off,
		  len,
		  prot_str,
		  flags);
	kassert(file != nullptr);

	struct fb_device* fbdev = file->private_data;
	kassert(fbdev != nullptr);

	sem_wait(&fbdev->sem);
	size_t vram_len = fbdev->vram_len;
	paddr_t vram_paddr = fbdev->vram_paddr;
	sem_signal(&fbdev->sem);

	if (!is_page_aligned((uptr)off) || off < 0 ||
	    (size_t)off + len > vram_len || flags & MAP_PRIVATE) {
		return -EINVAL;
	}

	struct address_space* vas = get_current_task()->vas;
	uptr start = align_down_page((uptr)addr);
	uptr end = align_up_page(start + len);
	int res = map_device_region(vas,
				    start,
				    end,
				    vram_paddr + (paddr_t)off,
				    (ulong)prot,
				    (ulong)flags);
	return res;
}

static void fb_get_screeninfo(struct fb_device* fbdev,
			      struct fb_screeninfo* info)
{
	sem_wait(&fbdev->sem);
	info->width = fbdev->width;
	info->height = fbdev->height;
	info->pitch = fbdev->pitch;
	info->bpp = fbdev->bpp;
	info->format = (uint32_t)fbdev->format;
	info->caps = fbdev->caps;
	info->vram_len = fbdev->vram_len;
	sem_signal(&fbdev->sem);
}

static int fb_set_mode(struct fb_device* fbdev, enum fb_mode mode)
{
	switch (mode) {
	case FB_MODE_TEXT:
		// Early return if we are already in the same mode
		if (fbdev->mode == mode) {
			return 0;
		}
		fbdev->mode = mode;
		screen_enable();
		term_resume_text();
		return 0;
	case FB_MODE_GRAPHICS:
		if (fbdev->mode == mode) {
			return 0;
		}
		fbdev->mode = mode;
		term_pause_text();
		screen_disable();
		return 0;
	default: return -EINVAL;
	}
}

int fb_ioctl(struct vfs_file* file, unsigned long request, void __user* arg)
{
	kassert(file != nullptr);

	struct fb_device* fbdev = file->private_data;
	kassert(fbdev != nullptr);

	log_debug("Processing framebuffer request: %s (0x%lx)",
		  __get_fb_ioctl_request_name(request),
		  request);

	switch (request) {
	case FBIOGET_SCREENINFO: {
		struct fb_screeninfo info = { 0 };
		fb_get_screeninfo(fbdev, &info);
		return (int)copy_to_user(arg,
					 &info,
					 sizeof(struct fb_screeninfo));
	}
	case FBIOSET_MODE: {
		enum fb_mode mode = 0;
		long res = copy_from_user(&mode, arg, sizeof(mode));
		if (res < 0) {
			return (int)res;
		}
		log_debug("Setting mode to %s", __get_fb_mode_name(mode));
		sem_wait(&fbdev->sem);
		res = fb_set_mode(fbdev, mode);
		sem_signal(&fbdev->sem);
		return (int)res;
	}
	default: return -ENOTTY; // Invalid request
	}
}
