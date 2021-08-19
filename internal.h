#pragma once
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
#include <linux/sched.h>
#include <linux/mm.h>
#include "channel.h"
#include "config.h"

#if DSM_PRINT_DEBUG
#define dsm_debug(fmt, ...) printk(KERN_INFO "%d:%s:%d:DSMFS " fmt,		\
		current->pid, __func__, __LINE__, ##__VA_ARGS__)
#else
#define dsm_debug(fmt, ...) /**/
#endif

#define dsm_print(fmt, ...) printk(KERN_INFO "%d:%s:%d:DSMFS " fmt, current->pid, __func__, __LINE__, ##__VA_ARGS__)

#if DSM_TIME
#define dsm_time(stepname) printk(KERN_INFO "%d:%s:%d:DSMTRACE %s\n",		\
		current->pid, __func__, __LINE__, stepname)
//#define dsm_time(stepname) printk(KERN_INFO "%d:%s:%d:DSMTIME %s", current->pid, __func__, __LINE__, ##__VA_ARGS__)
#else
#define dsm_time(stepname) /**/
#endif

struct dsmfs_mount_opts {
	umode_t mode;
};

struct dsmfs_fs_info {
	int ino_gen;
	int server_id;//is the port
	char rip[MAX_NODES][IP_MAX_SIZE];// remote ip (we need a list)
	int rport;// remote port (we need a list)
	dsm_channel_t * server_channel;
//#ifdef TEST_DSM 
	dsm_channel_t * ktcp_server;
//#endif
	#define NUM_SERVER 3
	struct task_struct *read_server[NUM_SERVER];//server thread
	struct task_struct *write_server[NUM_SERVER];//server thread
	struct task_struct *inval_server[NUM_SERVER];//server thread
	struct dsmfs_mount_opts mount_opts;
};

//extern const struct inode_operations ramfs_file_inode_operations;

int dsmfs_fill_page(struct inode *inode, struct page *page);

int dsmfs_upgrade_page(struct inode *inode, struct page *page);

int dsmfs_server_init(struct super_block *sb);

void dsmfs_server_destroy(struct dsmfs_fs_info *fsi);
