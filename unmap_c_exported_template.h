static typeof(&ptep_clear_flush) ptep_clear_flush_p = (void*)(ptep_clear_flush_place_holder);
static typeof(&rmap_walk) rmap_walk_p = (void*)(rmap_walk_place_holder);
#define rmap_walk rmap_walk_p
//static typeof(&__mmu_notifier_invalidate_range_start) __mmu_notifier_invalidate_range_start_p = (void*)(__mmu_notifier_invalidate_range_start_place_holder);
//#define __mmu_notifier_invalidate_range_start __mmu_notifier_invalidate_range_start_p
static typeof(&__mmu_notifier_invalidate_range) __mmu_notifier_invalidate_range_p = (void*)(__mmu_notifier_invalidate_range_place_holder);
#define __mmu_notifier_invalidate_range __mmu_notifier_invalidate_range_p
static typeof(&__get_locked_pte) __get_locked_pte_p = (void*)(__get_locked_pte_place_holder);
#define __get_locked_pte __get_locked_pte_p
