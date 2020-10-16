/* internal.h: ramfs internal definitions
 *
 * Copyright (C) 2005 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/mm.h>
#include "channel.h"

struct dsmfs_mount_opts {
	umode_t mode;
};

struct dsmfs_fs_info {
	int ino_gen;
	int server_id;//is the port
	#define IP_MAX_SIZE 45
	char rip[IP_MAX_SIZE];// remote ip (we need a list)
	short rport;// remote port (we need a list)
	dsm_channel_t * server_channel;
	struct task_struct *thread;//server thread
	struct dsmfs_mount_opts mount_opts;
};

extern const struct inode_operations ramfs_file_inode_operations;

int dsmfs_fill_page(struct inode *inode, struct page *page);

int dsmfs_upgrade_page(struct inode *inode, struct page *page);

int dsmfs_server_init(struct super_block *sb);
void dsmfs_server_destroy(struct dsmfs_fs_info *fsi);
