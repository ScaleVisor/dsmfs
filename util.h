#ifndef _LINUX_DSMFS_H
#define _LINUX_DSMFS_H

#include <linux/fs.h>

struct inode *dsmfs_get_inode(struct super_block *sb, const struct inode *dir,
	 umode_t mode, dev_t dev);
extern struct dentry *dsmfs_mount(struct file_system_type *fs_type,
	 int flags, const char *dev_name, void *data);

#ifdef CONFIG_MMU
static inline int
dsmfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize)
{
	return 0;
}
#else
extern int dsmfs_nommu_expand_for_mapping(struct inode *inode, size_t newsize);
#endif

extern const struct file_operations dsmfs_file_operations;
extern const struct vm_operations_struct generic_file_vm_ops;
extern int init_dsmfs_fs(void);
extern int end_dsmfs_fs(void);

int dsmfs_fill_super(struct super_block *sb, void *data, int silent);

#endif
