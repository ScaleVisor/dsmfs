/* Copyright (C) - All Rights Reserved
 * Unauthorized copying of this file, via any medium is strictly prohibited
 * Proprietary and confidential
 * Written by Mohamed Lamine Karaoui <moharaka@gmail.com>, November 2020
 */

#include "internal.h"
//arch/x86/include/asm/pgtable.h
#include <linux/rmap.h>
#include <linux/mmu_notifier.h>

int define_event(int is_not_read){
	return is_not_read ? MMU_NOTIFY_UNMAP : MMU_NOTIFY_PROTECTION_PAGE
}
static int dsm_page_unmap_one(struct page *page, struct vm_area_struct *vma,
			    unsigned long address, void *arg)
{
	struct mm_struct *mm = vma->vm_mm;
	pte_t *pte;
	spinlock_t *ptl;
	int ret = 0;
	int clear_read = (int) (long)arg;
	struct mmu_notifier_range range;
	dsm_debug("curent vma owner's pid %d\n", vma->vm_mm->owner->pid);
	
	pte = page_check_address(page, mm, address, &ptl, 1);
	if (!pte)
		goto out;
	
	//INIT MEMORY NOTIFIER RANGE
	mmu_notifier_range_init(&range, define_event(clear_read),
				0, vma, mm, address,vma_address_end(page, vma));

	mmu_notifier_invalidate_range_start(&range);

	if (pte_write(*pte) || pte_present(*pte)) {
		pte_t entry;

		flush_cache_page(vma, address, pte_pfn(*pte));
		//nuke the pte: entry contains a copy of the old value
		entry = ptep_clear_flush(vma, address, pte);
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
	mmu_notifier_invalidate_range_end(&range);
/* 
	if (ret) {
		if(!clear_read){
			mmu_notifier_write_protect_page(mm, address);		
			//printk(KERN_DEBUG "WRITE PROTECT COUNT %ld", ++counter);
		}else{
			mmu_notifier_invalidate_page(mm, address);
			//printk(KERN_DEBUG "ZAP COUNT %ld", ++zapcounter);
		}
	} */
out:
	return SWAP_AGAIN;
}
static bool dsm_invalid_unmap_vma(struct vm_area_struct *vma, void *arg)
{
	dsm_debug("curent vma owner's pid %d\n", vma->vm_mm->owner->pid);
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
