/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "drivers/device.h"
#include "kernel/semaphores.h"
#include "kernel/uaccess.h"

#include <uapi/helios/fb.h>

struct fb_device {
	u32 width; // visible pixels
	u32 height;
	u32 pitch; // bytes per scanline
	u32 bpp;   // bits per pixel
	enum fb_format format;

	/* memory region */
	paddr_t vram_paddr;
	size_t vram_len;

	u32 caps; // capabilities bitmask (FB_CAP_*)

	enum fb_mode mode;

	semaphore_t sem;
	struct chrdev cdev;
	struct file_ops* fops;
};

void fb_init();

ssize_t fb_write(struct vfs_file* file,
		 const char __user* buffer,
		 size_t count,
		 off_t* offset);

int fb_mmap(struct vfs_file* file,
	    void* addr,
	    size_t len,
	    int prot,
	    int flags,
	    off_t off);

int fb_ioctl(struct vfs_file* file, unsigned long request, void __user* arg);
