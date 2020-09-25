#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include "util.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MLK");	
MODULE_DESCRIPTION("DSMFS");
MODULE_VERSION("0.01");

static int __init dsmfs_init(void) {
	printk(KERN_INFO "Loading DSMFS !\n");
	init_dsmfs_fs();
	return 0;
}

static void __exit dsmfs_exit(void) {
	printk(KERN_INFO "Unloading DSMFS !\n");
}

module_init(dsmfs_init);
module_exit(dsmfs_exit);
