% FB(4) HeliOS Special Files | Helios Manual

# NAME

fb - framebuffer device

# SYNOPSIS

/dev/fb

# DESCRIPTION

`/dev/fb` is a character device exposing the boot framebuffer handed to the kernel by
Limine. There is exactly one instance — geometry is fixed at boot (`fb_init()`,
`drivers/fb.c`) from `limine_framebuffer` and never changes at runtime. There is no mode
setting; `width`, `height`, `pitch`, `bpp`, and `format` are read-only for the lifetime of
the system.

*Naming note:* the node is `/dev/fb` today. A rename to `/dev/fb0` is tracked separately
(issue #08, to make room for a second output later) — check `devfs_map_name()`'s call in
`fb_init()` if this page and the running kernel disagree.

# IOCTLS

`FBIOGET_SCREENINFO`
:   `arg` is `struct fb_screeninfo *` (`uapi/helios/fb.h`). Fills in the device's fixed
    geometry:

    ```c
    struct fb_screeninfo {
            uint32_t width;    /* visible pixels */
            uint32_t height;
            uint32_t pitch;    /* bytes per scanline */
            uint32_t bpp;      /* bits per pixel */
            uint32_t format;   /* enum fb_format */
            uint32_t caps;     /* enum fb_caps bitmask */
            uint64_t vram_len; /* total mappable bytes */
    };
    ```

    **Always use `pitch` to index into VRAM, never `width * bpp / 8`.** Limine does not
    guarantee the two are equal, and assuming so produces an image that shears
    diagonally — a distinctive symptom if you see it.

    `format` is currently always `FB_FMT_XRGB8888` (32bpp, little-endian, X-R-G-B) — the
    driver does not yet read Limine's channel masks, so this is a hardcoded fact about
    QEMU/Limine's current output, not a negotiated value. Do not assume other formats work.

    `caps` is a bitmask of `enum fb_caps`. Only `FB_CAP_MMAP` is ever set; the rest
    (`FB_CAP_PAN`, `FB_CAP_VBLANK_IRQ`, `FB_CAP_SET_MODE`, `FB_CAP_FLUSH_RECT`) are
    declared but never advertised because the driver does not implement them — do not
    probe for them expecting a future flip to on.

Any other request returns `-1` with `errno` set to `ENOTTY`.

# WRITE

`write(fd, buf, count)` copies up to `count` bytes (clamped to `vram_len`) starting at
byte offset `*offset` in VRAM — see `write(2)`/`fb_write()` for the exact offset handling.
This is a simple, synchronous blit path; there is no partial-scanline convenience beyond
what raw byte offsets give you. For anything beyond a one-shot full-frame write, `mmap`
(below) is the intended interface.

# MMAP

`/dev/fb` implements `.mmap` (`fb_mmap()`), so it can be mapped with `mmap(2)`:

```c
void* vram = mmap(nullptr, info.vram_len, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
```

- `MAP_SHARED` only — `MAP_PRIVATE` is rejected. A private, copy-on-write mapping of a
  physical device makes no sense and `map_device_region()` (#05) rejects it independently
  of the driver.
- `offset` must be page-aligned and `offset + length` must fit within `vram_len`.
- On success the returned pointer aliases physical VRAM directly (`MR_DEVICE`, no struct
  page backing, mapped eagerly rather than demand-paged).

## The mapping is write-combining — draw into a back buffer

VRAM is mapped WC (write-combining), the same policy the kernel's own framebuffer console
uses (`vmm.c`). This makes sequential writes to VRAM fast but **reads from VRAM slow** —
close to uncached-read speed. This is the single most important performance fact about
this device:

**Draw into a normal (`PROT_READ | PROT_WRITE`, ordinary WB anonymous memory) back buffer,
then `memcpy` the finished frame across to the VRAM mapping.** Never read back from the
VRAM mapping to composite against — that turns every pixel operation into a slow
uncached-style read. This is exactly the pattern any drawing library (`libgfx`, #14)
built on top of this device needs to get right.

## Not inherited across fork()

`vmm_fork_region()` returns `-ENOTSUP` for `MR_DEVICE` regions (`vmm.c`). A process
holding an active `/dev/fb` mapping that calls `fork()` will have the fork call fail.
This is deliberate — there is no sane way to share or duplicate a live device
mapping — not a bug. Map after any `fork()` you intend to do, not before.

## No vsync, no double buffering, tearing is expected

There is no vertical-blank interrupt, no page flipping, and no kernel-side double
buffering (`FB_CAP_VBLANK_IRQ` and `FB_CAP_PAN` are both unset, always). A `memcpy` into
VRAM can land mid-scanout. Visible tearing during fast updates is expected behavior, not
a driver bug.

# NOTES

**The console can draw over you.** `/dev/console` → `tty0` → the kernel's own terminal
renders through this same framebuffer via `screen.c`. Once `kernel_main` switches to
`LOG_KLOG`, the kernel stops writing to the screen *except* for the terminal cursor blink
timer, which reschedules itself forever regardless of anything a GUI app does — see #13
(`FBIOSET_MODE` / `term_pause_cursor()`), not yet implemented. Until that lands, expect a
flickering inverted cell somewhere on screen while your program runs. If your own program
inherits `/dev/console` on fds 0/1/2 (the default for anything launched from `hsh`) and
calls `printf`, that also draws text glyphs directly over your rendering — redirect or
avoid stdout while drawing.

**`panic()` bypasses all of this deliberately.** `panic()` calls `set_log_mode(LOG_DIRECT)`
before printing, so a kernel panic always becomes visible on screen even mid-GUI. A
screen suddenly full of text is a panic, not a rendering bug in your program.

**`munmap()` does not currently unmap anything** — see `mmap(2)` BUGS. A `/dev/fb` mapping
outlives the call that "removed" it; only process exit actually reclaims it today.

# SEE ALSO

mmap(2), ioctl(2), write(2)
