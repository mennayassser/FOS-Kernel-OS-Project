#include "kheap.h"

#include <inc/memlayout.h>
#include <inc/dynamic_allocator.h>
#include <kern/conc/sleeplock.h>
#include <kern/proc/user_environment.h>
#include <kern/mem/memory_manager.h>
#include "../conc/kspinlock.h"

extern uint32 dynAllocStart;
extern uint32 dynAllocEnd;
extern uint32 kheapPageAllocStart ;
extern uint32 kheapPageAllocBreak ;

struct kspinlock kernel_safety_lock;

#define MAX_PAGE_ALLOCS 10000

struct PageAllocInfo {
    uint32 va;
    uint32 pages;
};

static struct PageAllocInfo page_allocations[MAX_PAGE_ALLOCS];
static int page_alloc_count = 0;

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

//==============================================
// [1] INITIALIZE KERNEL HEAP:
//==============================================
//TODO: [PROJECT'25.GM#2] KERNEL HEAP - #0 kheap_init [GIVEN]
//Remember to initialize locks (if any)
void kheap_init()
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		initialize_dynamic_allocator(KERNEL_HEAP_START, KERNEL_HEAP_START + DYN_ALLOC_MAX_SIZE);
		set_kheap_strategy(KHP_PLACE_CUSTOMFIT);
		kheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		kheapPageAllocBreak = kheapPageAllocStart;
	}
	//==================================================================================
	//==================================================================================

	init_kspinlock(&kernel_safety_lock, "kheap");
	static struct PageAllocationData *allocated_pages_list_head = NULL;

}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = alloc_page(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE), PERM_WRITEABLE, 1);
	if (ret < 0)
		panic("get_page() in kern: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	unmap_frame(ptr_page_directory, ROUNDDOWN((uint32)va, PAGE_SIZE));
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//
//===================================
// [1] ALLOCATE SPACE IN KERNEL HEAP:
//===================================
//3andi kefaya frames fe el physical space ?(0 not enough / 1 enough)
int see_enough_frames(uint32 pages_needed)
{
	 uint32 total_available_frames = calculate_available_frames().freeBuffered + calculate_available_frames().freeNotBuffered ;

	 if(total_available_frames < pages_needed)
	 {
		 return 0;
	 }
	 else
	 {
		 return 1;
	 }
}

// nafs logic el user heap custom fit b ekhtelaf how it checks
// Custom Fit
void* custom_fit_search(uint32 size, uint32 start_address, uint32 end_address)
{
	uint32 number_of_pages = (ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE);
	uint32 current_va = start_address;
	uint32 gap_size, allocation_start, gap_start;
	uint32 *PageTable;


	//exact fit
		while (current_va < end_address)
		{
			allocation_start = current_va;
			struct FrameInfo* frame = get_frame_info(ptr_page_directory, current_va, &PageTable);

			if(frame != 0) {
				current_va += PAGE_SIZE;
				continue;
			}

			else {
				gap_size  = 0;
				uint32 temp = current_va;
				gap_start = current_va;

				while(temp < end_address) {
					    frame = get_frame_info(ptr_page_directory, temp, &PageTable);
					if(frame != 0)
						{
					    	break;
						}
					else
					{
							gap_size++;
							temp += PAGE_SIZE;
					}
				}
				if (gap_size == number_of_pages){
					allocation_start = gap_start;
					return (void *)allocation_start;
				}

				current_va = temp;
			}
		}

	//worst fit (choose biggest gap, leave the most space behind)
		uint32 max_gap = 0;
		current_va = start_address;
		gap_start = 0;
		uint32 temp = current_va;

		while (current_va < end_address)
		{
			struct FrameInfo* frame = get_frame_info(ptr_page_directory, current_va, &PageTable);

			// check if a page is free by checking if it is mapped to a frame
			if (frame != 0){
				current_va += PAGE_SIZE;
				continue;
			}
			else
			{
				uint32 gap_start = current_va;
				gap_size = 0;
				uint32 temp = current_va;

				while (temp < end_address)
				{
					frame = get_frame_info(ptr_page_directory, temp, &PageTable);
					if (frame != 0)
					{
						break;
					}
					gap_size++;
					temp += PAGE_SIZE;
				}
					if ((gap_size > max_gap) && (gap_size > number_of_pages))
					{
						max_gap = gap_size;
						allocation_start = gap_start;
					}

					current_va = temp;

			}

		}

		// only return worst fit gap if it is >= num of pages needed
		if (max_gap > number_of_pages)
		{
			return (void *)allocation_start;
		}

		//if none work, return null (break extension handled in kmalloc)
		return NULL;

}


void* kmalloc(unsigned int size)
{

	    if (size == 0)
	    	return NULL;

	    // allocate block
	    if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	        return alloc_block(size);

	    acquire_kspinlock(&kernel_safety_lock);

	    uint32 pages = ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE;
	    uint32 alloc_size = pages * PAGE_SIZE;

	    // custom fit returns address kmalloc starts allocating from
	    uint32 start_address = (uint32)custom_fit_search(alloc_size, kheapPageAllocStart, kheapPageAllocBreak);

	    // if custom fit returned 0, check if there is enough space in unused
	    if (start_address == 0) {
	        uint32 remaining = KERNEL_HEAP_MAX - kheapPageAllocBreak;
	        // if there isn't, return NULL
	        if (remaining < alloc_size) {
	            release_kspinlock(&kernel_safety_lock);
	            return NULL;
	        }
	        // move break to take from unused
	        start_address = kheapPageAllocBreak;
	        kheapPageAllocBreak += alloc_size;
	    }

	    // also check if there are enough frames in physical memory
	    if (!see_enough_frames(pages)) {
	        release_kspinlock(&kernel_safety_lock);
	        panic("Insufficient space in kernel memory!");
	    }

	    // allocate pages and add start address and alloc size to array "page_allocations"
	    for (uint32 i = 0; i < pages; i++)
	        get_page((void*)(start_address + i * PAGE_SIZE));

	    if (page_alloc_count < MAX_PAGE_ALLOCS) {
	        page_allocations[page_alloc_count].va = start_address;
	        page_allocations[page_alloc_count].pages = pages;
	        page_alloc_count++;
	    }
	    else {
	        release_kspinlock(&kernel_safety_lock);
	        panic("Max Possible Kernel Heap Allocations Exceeded!");
	    }

	    release_kspinlock(&kernel_safety_lock);
	    return (void*)start_address;
}

//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
//=================================
// [2] FREE SPACE FROM KERNEL HEAP:
//=================================
void kfree(void* virtual_address)
{
#if USE_KHEAP
	if (virtual_address == NULL) return;

	uint32 va = (uint32)virtual_address;
	uint32 page_start = ROUNDDOWN((uint32)virtual_address, PAGE_SIZE);

	// If address is in dynamic allocator region (small blocks)
	if ((page_start >= dynAllocStart) && (page_start < dynAllocEnd)) {
		free_block(virtual_address);
		return;
	}

	// Page allocation from kernel heap
	acquire_kspinlock(&kernel_safety_lock);

	// Find allocation in tracking array
	int idx = -1;
	for (int i = 0; i < page_alloc_count; i++) {
		if (page_allocations[i].va == page_start) {
			idx = i;
			break;
		}
	}

	if (idx == -1) {
		release_kspinlock(&kernel_safety_lock);
		panic("kfree: address not allocated by kmalloc");
	}

	uint32 pages = page_allocations[idx].pages;
	uint32 end_address = page_start + (pages * PAGE_SIZE);

	// Unmap all pages from page directory
	for (uint32 i = 0; i < pages; i++)
		unmap_frame(ptr_page_directory, page_start + i * PAGE_SIZE);

	// Remove from tracking array (swap with last element)
	page_alloc_count--;
	page_allocations[idx] = page_allocations[page_alloc_count];

	// If freeing memory at the end, try to compact heap
	if (end_address == kheapPageAllocBreak)
	{
		// Find new break position (highest allocated address + 1)
		uint32 new_break = kheapPageAllocStart;
		for (int i = 0; i < page_alloc_count; i++)
		{
			uint32 allocation_end = page_allocations[i].va +
				(page_allocations[i].pages * PAGE_SIZE);
			if (allocation_end > new_break)
				new_break = allocation_end;
		}

		new_break = ROUNDUP(new_break, PAGE_SIZE);

		// Move break pointer back if possible
		if (new_break < kheapPageAllocBreak) {
			kheapPageAllocBreak = new_break;
		}
	}

	release_kspinlock(&kernel_safety_lock);

#endif
}

//=================================
// [3] FIND VA OF GIVEN PA:
//=================================
unsigned int kheap_virtual_address(unsigned int physical_address)
{
#if USE_KHEAP
	// Extract frame number (shift right by 12 bits = divide by 4096)
	uint32 frame_number = physical_address >> 12;
	// Extract offset within frame (last 12 bits)
	uint32 offset = physical_address & 0xFFF;

	// Use reverse page table: frame_number -> virtual page
	uint32 page_number = reverse_page_table[frame_number];

	if (page_number == 0) {
		return 0;  // Frame not mapped to any virtual page
	}
	uint32 virtual_address = page_number + offset;

	return virtual_address;
#endif
	panic("USE_KHEAP Not Set to 1");
	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */
}

//=================================
// [4] FIND PA OF GIVEN VA:
//=================================
unsigned int kheap_physical_address(unsigned int virtual_address)
{
#if USE_KHEAP
	uint32 *PageTable;
	// Extract page table index (bits 12-21 of virtual address)
	uint32 PageNumber = PTX(virtual_address);
	// Extract offset within page (last 12 bits)
	uint32 offset = virtual_address & 0xFFF;


	int page_table = get_page_table(ptr_page_directory, virtual_address, &PageTable);

	if (page_table != TABLE_IN_MEMORY){
		return 0;  // Page table doesn't exist
	}

	// Get page table entry
	uint32 *entry = &PageTable[PageNumber];
	if (!(*entry & 0x1)) {
		return 0; 	}

	// Extract frame address from entry (bits 12-31)
	uint32 frame_address = EXTRACT_ADDRESS(*entry);
	// Combine frame base address with offset
	uint32 physical_address = frame_address + offset;

	return physical_address;
#endif
	panic("USE_KHEAP Not Set to 1");
	/*EFFICIENT IMPLEMENTATION ~O(1) IS REQUIRED */
}

//=================================================================================//
//============================== BONUS FUNCTION ===================================//
//=================================================================================//
// krealloc():

//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to kmalloc().
//	A call with new_size = zero is equivalent to kfree().

extern __inline__ uint32 get_block_size(void *va);

void *krealloc(void *virtual_address, uint32 new_size)
{
	//TODO: [PROJECT'25.BONUS#2] KERNEL REALLOC - krealloc
	//Your code is here
	//Comment the following line
	//panic("krealloc() is not implemented yet...!!");

#if USE_KHEAP
	uint32 KERNEL_HEAP_MEM_SIZE = KERNEL_HEAP_MAX - KERNEL_HEAP_START;
	uint32 new_pages = ROUNDUP(new_size, PAGE_SIZE) / PAGE_SIZE;
	uint32 va = (uint32)virtual_address;

	if (new_size > KERNEL_HEAP_MEM_SIZE)
	{
		return 0;
	}
	if (virtual_address == NULL)
	{
		return kmalloc(new_size);
	}
	if (new_size == 0)
	{
		kfree(virtual_address);
		return NULL;
	}

	if (new_size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{
		return realloc_block(virtual_address, new_size);
	}
	else
	{
		acquire_kspinlock(&kernel_safety_lock);

		uint32 old_size;
		int idx = -1;
		for (int i = 0; i < page_alloc_count; i++) {
			if (page_allocations[i].va == va) {
				idx = i;
				break;
			}
		}

		if (idx == -1) {
			release_kspinlock(&kernel_safety_lock);
			panic("krealloc: address not allocated by kmalloc");
		}

		uint32 old_pages = page_allocations[idx].pages;
		uint32 old_start_address = page_allocations[idx].va;
		old_size = old_pages * PAGE_SIZE;

		if (old_size == new_size)
		{
			release_kspinlock(&kernel_safety_lock);
			return (void *)old_start_address;
		}

		if (new_size < old_size)
		{

			page_allocations[idx].pages = new_pages;

			uint32 shrink = old_size - new_size;
			shrink = ROUNDUP(shrink, PAGE_SIZE);
			uint32 pages_to_free = shrink / PAGE_SIZE;
			uint32 freeing_start = old_start_address + (new_pages * PAGE_SIZE);

			if (page_alloc_count < MAX_PAGE_ALLOCS) {
				page_allocations[page_alloc_count].va = freeing_start;
				page_allocations[page_alloc_count].pages = pages_to_free;
			}
			release_kspinlock(&kernel_safety_lock);

			kfree((void *)freeing_start);

			return(void *)old_start_address;

		}
		else
		{
			uint32 expand_by = new_size - old_size;
			uint32 new_pages = ROUNDUP(new_size, PAGE_SIZE) / PAGE_SIZE;
	        uint32 old_end = page_allocations[idx].va + (page_allocations[idx].pages * PAGE_SIZE);
	        uint32 old_pages = page_allocations[idx].pages;
	        uint32 pages_needed = new_pages - old_pages;
	        uint32 old_start = page_allocations[idx].va;

	        uint32 current_va = old_end;
	        uint32 free_consec = 0;
	        uint32 *PageTable;


	        	while ((current_va < kheapPageAllocBreak) && (free_consec < pages_needed))
				{
					struct FrameInfo* frame = get_frame_info(ptr_page_directory, current_va, &PageTable);

					// check if a page is free by checking if it is mapped to a frame
					if (frame != 0){
						break;
					}
					free_consec++;
					current_va += PAGE_SIZE;
				}


	        	if (free_consec >= pages_needed)
	        	{
	        		if (!see_enough_frames(pages_needed))
	        		{
		        		release_kspinlock(&kernel_safety_lock);
		        		return NULL;
	        		}
	        		for (uint32 m = 0; m < pages_needed ; m++)
	        		{
	        	        get_page((void*)(old_end + m * PAGE_SIZE));
	        		}

	        		page_allocations[idx].pages = new_pages;


	        		release_kspinlock(&kernel_safety_lock);
	        		return (void *)old_start;
	        	}

	    		release_kspinlock(&kernel_safety_lock);



	        	void * alloc = kmalloc(new_size);
	        	if (alloc == NULL)
	        	{
	        		return alloc;
	        	}
	        	kfree((void *)old_start);
		}


		release_kspinlock(&kernel_safety_lock);

	}
	return NULL;
#endif
	return NULL;

}
