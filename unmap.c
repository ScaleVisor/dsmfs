/* Copyright (C) - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mohamed Lamine Karaoui <moharaka@gmail.com>, November 2020
 */


#include "internal.h"
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>
#include <asm-generic/cacheflush.h>
#include "unmap_c_exported.h"

static inline pte_t *get_locked_pte_local(struct mm_struct *mm, unsigned long addr,
				    spinlock_t **ptl)
{
	pte_t *ptep;
	__cond_lock(*ptl, ptep = __get_locked_pte(mm, addr, ptl));
	return ptep;
}

static inline void mmu_notifier_invalidate_range_local(struct mm_struct *mm,
		                                  unsigned long start, unsigned long end)
{
	if (mm_has_notifiers(mm))
		__mmu_notifier_invalidate_range(mm, start, end);
}

static bool dsm_page_unmap_one(struct page *page, struct vm_area_struct *vma,
			    unsigned long address, void *arg)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte;
	spinlock_t *ptl;
	int ret = 0;
	int clear_read = (int) (long)arg;

	//dsm_debug("curent vma owner's pid %d\n", vma->vm_mm->owner->pid);

	//pte = page_check_address(page, mm, address, &ptl, 1);
	pte = get_locked_pte_local(mm, address, &ptl);
	if (!pte)
		goto out;

	if (pte_write(*pte) || pte_present(*pte)) {
		pte_t entry;

		flush_cache_page(vma, address, pte_pfn(*pte));
		//nuke the pte: entry contains a copy of the old value
		entry = ptep_clear_flush_p(vma, address, pte);
		if(!clear_read)
		{
			/* keep the entry read only */
			entry = pte_wrprotect(entry);
			entry = pte_mkclean(entry);
			set_pte_at(mm, address, pte, entry);
		}
		ret = 1;
	}

	pte_unmap_unlock(pte, ptl);

	if (ret) {
		printk(KERN_INFO "%d: page cleaned %ld %p %ld %p %lx %lx\n", 
				((struct dsmfs_fs_info*) page->mapping->host->i_sb->s_fs_info)->server_id, 
				page->mapping->host->i_ino,
				page, page->index, page_to_virt(page), address, address+PAGE_SIZE);
		mmu_notifier_invalidate_range_local(mm, address, address+PAGE_SIZE);
		dsm_debug("page cleaned %ld\n", page->index);
	}
out:
	return true;
}

static bool dsm_invalid_unmap_vma(struct vm_area_struct *vma, void *arg)
{
	//dsm_debug("curent vma owner's pid %d\n", vma->vm_mm->owner->pid);
	return false;
}


int dsm_page_unmap(struct page *page, int clear_read)
{
	struct address_space *mapping;
	struct rmap_walk_control rwc = {
		.arg = (void *)(long)clear_read,
		.rmap_one = dsm_page_unmap_one,
		.invalid_vma = dsm_invalid_unmap_vma,
	};

	//BUG_ON(!PageLocked(page));

	if (!page_mapped(page))
		return -EIO;

	mapping = page_mapping(page);
	if (!mapping)
		return -EINVAL;

	rmap_walk(page, &rwc);

	return 0;
}
