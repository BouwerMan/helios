/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

enum FIELD_LENGTHS {
	TARFS_NAME_SIZE = 100,
	TARFS_MODE_SIZE = 8,
	TARFS_UID_SIZE = 8,
	TARFS_GID_SIZE = 8,
	TARFS_SIZE_SIZE = 12,
	TARFS_MTIME_SIZE = 12,
	TARFS_CHECKSUM_SIZE = 8,
	TARFS_TYPEFLAG_SIZE = 1,
	TARFS_LINKNAME_SIZE = 100,
	TARFS_MAGIC_SIZE = 6,
	TARFS_VERSION_SIZE = 2,
	TARFS_OWNER_SIZE = 32,
	TARFS_GROUP_SIZE = 32,
	TARFS_DEVMAJOR_SIZE = 8,
	TARFS_DEVMINOR_SIZE = 8,
	TARFS_PREFIX_SIZE = 155,
};

struct ustar_header {
	char name[100];
	char mode[8];
	char uid[8];
	char gid[8];
	char size[12]; // Size is an octal string
	char mtime[12];
	char checksum[8];
	char typeflag;
	char linkname[100];
	char magic[6]; // Should be "ustar"
	char version[2];
	char owner[32];
	char group[32];
	char devmajor[8];
	char devminor[8];
	char prefix[155];
} __attribute__((packed));

// TODO: Should probably move this to an initramfs kind of file
void unpack_tarfs(void* archive_address);
