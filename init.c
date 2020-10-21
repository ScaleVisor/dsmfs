#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include "util.h"
#include "internal.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("MLK");	
MODULE_DESCRIPTION("DSMFS");
MODULE_VERSION("0.01");


static char *path_param;
module_param(path_param, charp, 0);


static int __init dsmfs_init(void) {

	dsm_print("Loading DSMFS !\n");
	init_dsmfs_fs();

	return 0;
}

static void __exit dsmfs_exit(void) {
	dsm_print("Unloading DSMFS !\n");
	end_dsmfs_fs();
}

module_init(dsmfs_init);
module_exit(dsmfs_exit);
