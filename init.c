#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "util.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MLK");	
MODULE_DESCRIPTION("DSMFS");
MODULE_VERSION("0.01");

static int server_id;
module_param(server_id,int,0660);

static char *path_param;
module_param(path_param, charp, 0);


static int __init dsmfs_init(void) {
	int err;
	struct path pth;
	struct dentry *dtr;
	struct inode *inode;

	printk(KERN_INFO "Loading DSMFS !\n");


	//check path param
	if (path_param)
		pr_info("The parameter is %s\n", path_param);
	else {
		pr_info("The parameter is empty");
		goto out;
	}

	/* Get path */
	err = kern_path(path_param, 0, &pth);
	if (err)
		goto out;

	/* Get dentry */
	dtr = pth.dentry;
	BUG_ON(dtr == NULL);

	/* Print dtr dtr->inode info */
	spin_lock(&dtr->d_lock);
	if(!d_really_is_positive(dtr))
	{
		pr_info("Negative inode!\n");
		spin_unlock(&dtr->d_lock);
		path_put(&pth);
		goto out;
	}

	inode=d_inode(dtr);
	inode = igrab(inode);
	BUG_ON(!inode);
	pr_info("The inode address is %pK\n", inode);
	pr_info("The inode number is %lu\n", inode->i_ino);
	pr_info("The inode size is %lld\n", i_size_read(inode));
	pr_info("Dentry name %s\n", dtr->d_name.name);
	spin_unlock(&dtr->d_lock);
	path_put(&pth);
	/* Done printing dtr info */

	init_dsmfs_fs();

	return 0;
out:
	return -1;
}

static void __exit dsmfs_exit(void) {
	printk(KERN_INFO "Unloading DSMFS !\n");
	end_dsmfs_fs();
}

module_init(dsmfs_init);
module_exit(dsmfs_exit);
