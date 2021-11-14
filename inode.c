/* Copyright (C) - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mohamed Lamine Karaoui <moharaka@gmail.com>, November 2020
 */


//FIXME: rewrite copied parts
/*
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include "internal.h"
#include "util.h"
#include "inode_c_exported.h"

#define DSMFS_DEFAULT_MODE	0755
#define DSMFS_MAGIC		0x9A8458f6

static const struct super_operations dsmfs_ops;
static const struct inode_operations dsmfs_dir_inode_operations;


enum {
	Opt_mode,
	Opt_port,
	Opt_ip,
	Opt_id,
	Opt_err
};

static const match_table_t tokens = {
	{Opt_mode, "mode=%o"},
	{Opt_port, "port=%u" },
	{Opt_ip, "ip=%s" },
	{Opt_id, "id=%s" },
	{Opt_err, NULL}
};


static int simple_readpage_wrapper(struct file *file, struct page *page)
{
	int ret;
	dsm_debug("%s DSMFS: page to fill %p, %ld, %p\n", 
				__func__, page, page->index, page->mapping);
	//dsmfs_fill_page(page->mapping->host, page); moved to fault
	ret=simple_readpage(file, page);
	return ret;
}

void dsm_notify_access_page(struct inode *inode, struct page *page, int write);
static int simple_write_begin_wrapper(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned flags,
			struct page **pagep, void **fsdata)
			//(struct file *file, struct page *page)
{
	int ret;
	struct page *page;
	struct inode *inode;

	ret=simple_write_begin(file, mapping, pos, len, flags, pagep, fsdata);
	if(!ret)
	{
		page = *pagep;
		//update dsmfs private data: writen pages are valid in node 0
		inode = page->mapping->host;
		dsm_debug("%s DSMFS: inode %ld  page %p, %ld, %p\n", 
				__func__, inode->i_ino, page, page->index, page->mapping);
		dsm_notify_access_page(inode, page, 1);
	}else
		dsm_debug("%s DSMFS: error %d\n", __func__, ret); 

	//dsmfs_upgrade_page(pagep->mapping->host, page); moved to fault ?
	
	return ret;
}

static const struct address_space_operations dsmfs_aops = {
	/* To explore for freeing some pages ........... *
	 * .writepage, writepages, releasepage, freepage */
	.readpage	= simple_readpage_wrapper,
	.write_begin	= simple_write_begin_wrapper,
	.write_end	= simple_write_end,
	.set_page_dirty	= __set_page_dirty_no_writeback_c,
};
extern const struct inode_operations dsmfs_file_inode_operations;

static int dsmfs_get_next_ino(struct super_block *sb)
{
	return ((struct dsmfs_fs_info*)sb->s_fs_info)->ino_gen++;
}

struct inode *dsmfs_get_inode(struct super_block *sb,
				const struct inode *dir, umode_t mode, dev_t dev)
{
	struct xarray *xarr;
	struct inode * inode = new_inode(sb);
	dsm_debug("%s DSMFS: !\n", __func__);


	if (inode) {
		inode->i_ino = dsmfs_get_next_ino(sb);//get_next_ino();
		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &dsmfs_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
		inode->i_private = NULL;//FIXME: check it is ok
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_op = &dsmfs_file_inode_operations;
			inode->i_fop = &dsmfs_file_operations;
			xarr = kzalloc(sizeof(struct xarray), GFP_KERNEL);//TODO: free
			xa_init(xarr);
			inode->i_private = xarr;
			//TODO: destroy the inode
			break;
		case S_IFDIR:
			inode->i_op = &dsmfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;

			/* directory inodes start off with i_nlink == 2 (for "." entry) */
			inc_nlink(inode);
			break;
		case S_IFLNK:
			inode->i_op = &page_symlink_inode_operations;
			inode_nohighmem(inode);
			break;
		}
		//TODO: remove inode at destroy?!!!!
		insert_inode_hash(inode);
#if 0
		if (insert_inode_locked(inode) < 0) {
			dsm_print("error insering inode!!!!!!!!!!!!!!!!!!!!! %p", inode);
			//err = -EIO;
			return NULL;//TODO: free inode+ better handling
		}
#endif
	}
	dsm_debug("%s DSMFS: !\n", __func__);
	return inode;
}

/*
 * File creation. Allocate an inode, and we're done..
 */
/* SMP-safe */
static int
dsmfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode * inode = dsmfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;
	//dsm_debug("%s DSMFS: %s:%ld  state %ld !\n", __func__, dentry->d_name.name, inode->i_ino, inode->i_state);
	printk(KERN_INFO "%s DSMFS: %s:%ld  state %ld  mode %x!\n", __func__, 
			dentry->d_name.name, inode->i_ino, inode->i_state, mode);

	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);	/* Extra count - pin the dentry in core */
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}
	return error;
}

static int dsmfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
	int retval = dsmfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int dsmfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return dsmfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int dsmfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct inode *inode;
	int error = -ENOSPC;

	inode = dsmfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
	if (inode) {
		int l = strlen(symname)+1;
		error = page_symlink(inode, symname, l);
		if (!error) {
			d_instantiate(dentry, inode);
			dget(dentry);
			dir->i_mtime = dir->i_ctime = current_time(dir);
		} else
			iput(inode);
	}
	return error;
}

static const struct inode_operations dsmfs_dir_inode_operations = {
	.create		= dsmfs_create,
	.lookup		= simple_lookup,
	.link		= simple_link,
	.unlink		= simple_unlink,
	.symlink	= dsmfs_symlink,
	.mkdir		= dsmfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= dsmfs_mknod,
	.rename		= simple_rename,
};

/*
 * Display the mount options in /proc/mounts.
 */
static int dsmfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct dsmfs_fs_info *fsi = root->d_sb->s_fs_info;

	if (fsi->mount_opts.mode != DSMFS_DEFAULT_MODE)
		seq_printf(m, ",mode=%o", fsi->mount_opts.mode);
	//FIXME: more options
	return 0;
}

static const struct super_operations dsmfs_ops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
	.show_options	= dsmfs_show_options,
};


static int dsmfs_parse_options(char *data, struct dsmfs_fs_info *fsi)
{
	struct dsmfs_mount_opts *opts;
	substring_t args[MAX_OPT_ARGS];
	int option;
	int token;
	char *p;

	opts = &fsi->mount_opts;
	opts->mode = DSMFS_DEFAULT_MODE;

	while ((p = strsep(&data, ",")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_mode:
			if (match_octal(&args[0], &option))
				return -EINVAL;
			opts->mode = option & S_IALLUGO;
			break;
		case Opt_id:
			if (match_int(&args[0], &option))
				return -EINVAL;
			fsi->server_id = (int) option;
			dsm_print("%s DSMFS: ID of the current manager: %d\n", 
								__func__, fsi->server_id);
			break;
		case Opt_port:
		{
			char *__port = args[0].from;
			int rc = kstrtouint(__port, 0, &fsi->rport);
			if (rc)
				dsm_print("%s DSMFS: error parsing port\n", __func__); 

			dsm_print("%s DSMFS: PORT of the central manager: %d\n", 
								__func__, fsi->rport);
			break;
		}
		case Opt_ip:
		{
			/* dsm_print("%s DSMFS: IP of central manager pinned to localhost (FIXME)\n", __func__);*/
			int index=0;
			char *ip = NULL;
			/*
			ip = strtok(args[0].from, ",");
			dsm_print("%s DSMFS: list of ips %s\n", __func__, args[0].from);
			while( ip != NULL ) 
			{
				dsm_print("%s IP of node %d is %s\n", __func__, index, ip);
				strlcpy(fsi->rip[index++], ip, IP_MAX_SIZE);
      				ip = strtok(NULL, ",");
   			}*/
			while ((ip = strsep(&args[0].from, ";")) != NULL) {
        			if (*ip == '\0') continue;
				dsm_print("%s IP of node %d is %s\n", __func__, index, ip);
        			//printf("%s\n", token);
				strlcpy(fsi->rip[index], ip, IP_MAX_SIZE);
				index++;
    			}

			break;
		}
		/*
		 * We might like to report bad mount options here;
		 * but traditionally dsmfs has ignored all mount options,
		 * and as it is used as a !CONFIG_SHMEM simple substitute
		 * for tmpfs, better continue to ignore other mount options.
		 */
		}
	}

	return 0;
}

int dsmfs_server_init(struct super_block *sb);

int dsmfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct dsmfs_fs_info *fsi;
	struct inode *inode;
	int err = 0;

	//save_mount_options(sb, data); FIXME: just comment?

	// Also allocates the inode number
	fsi = kzalloc(sizeof(struct dsmfs_fs_info), GFP_KERNEL);
	sb->s_fs_info = fsi;
	if (!fsi)
		return -ENOMEM;

	err = dsmfs_parse_options(data, fsi);
	if (err)
		goto exit_err;

	err = dsmfs_server_init(sb);
	if (err)
		goto exit_err;

	sb->s_maxbytes		= MAX_LFS_FILESIZE;
	sb->s_blocksize		= PAGE_SIZE;
	sb->s_blocksize_bits	= PAGE_SHIFT;
	sb->s_magic		= DSMFS_MAGIC;
	sb->s_op		= &dsmfs_ops;
	sb->s_time_gran		= 1;

	inode = dsmfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
	sb->s_root = d_make_root(inode);
	if (!sb->s_root)
		return -ENOMEM;


	return 0;
exit_err:
	kfree(fsi);
	return err;
}

struct dentry *dsmfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_nodev(fs_type, flags, data, dsmfs_fill_super);
}

static void dsmfs_kill_sb(struct super_block *sb)
{
	dsmfs_server_destroy(sb->s_fs_info);
	kfree(sb->s_fs_info);
	kill_litter_super(sb);
}

static struct file_system_type dsmfs_fs_type = {
	.name		= "dsmfs",
	.mount		= dsmfs_mount,
	.kill_sb	= dsmfs_kill_sb,
	.fs_flags	= FS_USERNS_MOUNT,
};

int init_dsmfs_fs(void)
{
	return register_filesystem(&dsmfs_fs_type);
}

int end_dsmfs_fs(void)
{
	return unregister_filesystem(&dsmfs_fs_type);
}
