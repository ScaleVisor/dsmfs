/* Copyright (C) - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mohamed Lamine Karaoui <moharaka@gmail.com>, November 2020
 */


//FIXME: rewrite copied parts
/* file-mmu.c: dsmfs MMU-based file operations
 *
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

#include "util.h"
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/sched.h>

#include "internal.h"

static unsigned long dsmfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

int dsmfs_open(struct inode *inode, struct file *file)
{
	if(!inode) {
		dsm_debug("%s no inode %d!\n", __func__, -1);
	}else {
		dsm_debug("sb %p inode %p num %ld, size %lld, state %ld!\n", inode->i_sb, inode, inode->i_ino, inode->i_size, inode->i_state);
	}
	return 0;
}


static int filemap_fault_wrapper(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int ret;
	struct inode *inode = file_inode(vma->vm_file);
	dsm_debug("%ld!\n", vmf->pgoff);  
	dsm_time("Entered");
	//dump_stack();
	ret=filemap_fault(vma, vmf);
	dsm_debug("ret %d page %p!\n", ret, vmf->page);  
	if(vmf->page)  
		dsmfs_fill_page(inode, vmf->page);
	dsm_time("Exited");
	return ret;
}

#if 0
static void filemap_map_pages_wrapper(struct fault_env *fe, pgoff_t start_pgoff, pgoff_t end_pgoff)
{
	dsm_debug("%ld %ld!\n", start_pgoff, end_pgoff);  
	//dump_stack();
	//filemap_map_pages(fe, start_pgoff, end_pgoff);
}
#endif

static int filemap_page_mkwrite_wrapper(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	int ret;
	struct page *page = vmf->page;
	struct inode *inode = file_inode(vma->vm_file);

	dsm_debug("%ld!\n", page->index*PAGE_SIZE);  
	dsm_time("Entered");
	//printk("%s:%d %d Entered\n", __func__, __LINE__, current->pid);
	//dump_stack();


	ret=filemap_page_mkwrite(vma, vmf);
	if(ret==VM_FAULT_LOCKED)
		dsmfs_upgrade_page(inode, page);

	//printk("%s:%d %d Exited\n", __func__, __LINE__, current->pid);
	dsm_time("Exited");
	return ret;

}

const struct vm_operations_struct dsmfs_file_vm_ops = {
	.fault		= filemap_fault_wrapper,
	.map_pages	= NULL, //filemap_map_pages_wrapper,
	.page_mkwrite	= filemap_page_mkwrite_wrapper,
};

/*
 * set up a mapping for shared memory segments
 */
static int dsmfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct address_space *mapping = file->f_mapping;

	if (!mapping->a_ops->readpage)
		return -ENOEXEC;
	file_accessed(file);
	vma->vm_ops = &dsmfs_file_vm_ops;
	//vma->vm_ops = &dsmfs_file_vm_ops;
	return 0;
}

const struct file_operations dsmfs_file_operations = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= dsmfs_mmap,
	//.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
	.get_unmapped_area	= dsmfs_mmu_get_unmapped_area,
	.open			= dsmfs_open,
};

const struct inode_operations dsmfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};
