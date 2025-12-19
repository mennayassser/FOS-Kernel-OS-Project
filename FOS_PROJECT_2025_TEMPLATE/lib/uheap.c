#include <inc/lib.h>
#include <inc/dynamic_allocator.h>
//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//

struct uspinlock uheap_safety_lock;

struct UH_Allocated_Pages
{
	uint32 allocation_start;
	uint32 allocation_size;
};

#define MAX_PAGES 10000

static struct UH_Allocated_Pages page_allocations[MAX_PAGES];
static int allocated_pages = 0;


//==============================================
// [1] INITIALIZE USER HEAP:
//==============================================
int __firstTimeFlag = 1;
void uheap_init()
{
	if(__firstTimeFlag)
	{
		initialize_dynamic_allocator(USER_HEAP_START, USER_HEAP_START + DYN_ALLOC_MAX_SIZE);
		uheapPlaceStrategy = sys_get_uheap_strategy();
		uheapPageAllocStart = dynAllocEnd + PAGE_SIZE;
		uheapPageAllocBreak = uheapPageAllocStart;

		init_uspinlock(&uheap_safety_lock, "User Heap", 1);

		__firstTimeFlag = 0;
	}
}

//==============================================
// [2] GET A PAGE FROM THE KERNEL FOR DA:
//==============================================
int get_page(void* va)
{
	int ret = __sys_allocate_page(ROUNDDOWN(va, PAGE_SIZE), PERM_USER|PERM_WRITEABLE|PERM_UHPAGE);
	if (ret < 0)
		panic("get_page() in user: failed to allocate page from the kernel");
	return 0;
}

//==============================================
// [3] RETURN A PAGE FROM THE DA TO KERNEL:
//==============================================
void return_page(void* va)
{
	int ret = __sys_unmap_frame(ROUNDDOWN((uint32)va, PAGE_SIZE));
	if (ret < 0)
		panic("return_page() in user: failed to return a page to the kernel");
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

// Custom Fit
// user heap custom fit checks if a page is free by checking the value of the PERM_UHPAGE bit
// for each page in user heap range.
// 1) exact fit returns after finding the first gap that's the same size as the needed num of pages
// 2) worst fit scans entire UH range and returns the biggest contiguous block of pages
// 3) extend break and take from unused spaces if there's enough space
// 4) return NULL, no space to allocate required size
void* custom_fit_search(uint32 size, uint32 start_address, uint32 end_address)
{
	// only allocate pages kamla
	uint32 number_of_pages = ((uint32)ROUNDUP(size, PAGE_SIZE) / PAGE_SIZE);

	uint32 current_va = start_address;
	uint32 gap_size, allocation_start, gap_start;

	//exact fit
		while (current_va < end_address)
		{
			allocation_start = current_va;
			uint32 entry = ((uint32 *)UVPT)[VPN(current_va)];

			//
			if (entry & PERM_UHPAGE){
				current_va += PAGE_SIZE;
				continue;
			}
			else {
				gap_size  = 0;
				uint32 temp = current_va;
				gap_start = current_va;
				while (temp < end_address)
				 {
					entry = ((uint32 *)UVPT)[VPN(temp)];
					if (entry & PERM_UHPAGE){
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

	//worst fit (choose the biggest possible gap)
		uint32 max_gap = 0;
		current_va = start_address;
		gap_start = 0;
		uint32 temp = current_va;

		while (current_va < end_address)
		{
			uint32 entry = ((uint32 *)UVPT)[VPN(current_va)];

			if (entry & PERM_UHPAGE){
				current_va += PAGE_SIZE;
				continue;
			}
			else
			{
				uint32 gap_start = current_va;
				gap_size = 0;
				uint32 temp = current_va;
				while (temp < end_address && !(((uint32 *)UVPT)[VPN(temp)] & PERM_UHPAGE))
				{
					gap_size++;
					temp += PAGE_SIZE;
					if ((gap_size > max_gap) && (gap_size > number_of_pages))
					{
						max_gap = gap_size;
						allocation_start = gap_start;
					}
					current_va = temp;

				}
			}

		}

		if (max_gap > number_of_pages)
		{
			return (void *)allocation_start;
		}

	    //extend break
		uint32 remaining_unused = USER_HEAP_MAX - uheapPageAllocBreak;
		uint32 total_size = number_of_pages * PAGE_SIZE;
		if (remaining_unused >= total_size)
		{
			allocation_start = uheapPageAllocBreak;
			uheapPageAllocBreak += total_size;
			return (void *)allocation_start;
		}


		//if none work, return null
		return NULL;

}

// add to allocated pages list
// save start address and size
void add_to_array(uint32 start_address, uint32 size)
{
	if (allocated_pages < MAX_PAGES)
	{
		// reuse nullified prev allocations (kano allocated then et3mlohom free) lw fe
		for (int i = 0; i < allocated_pages; i++)
		{
			if(page_allocations[i].allocation_start == 0)
			{
				page_allocations[i].allocation_start = start_address;
				page_allocations[i].allocation_size = size;
				return;
			}
		}

		// lw mafesh allocate new spot
		page_allocations[allocated_pages].allocation_start = start_address;
		page_allocations[allocated_pages].allocation_size = size;
		allocated_pages++;
	}
}

uint32 find_page_size(uint32 start_address)
{
	for (int i = 0; i < MAX_PAGES; i++)
	{
		if(page_allocations[i].allocation_start == start_address)
			return page_allocations[i].allocation_size;
	}
	panic("Inserted Address Not Allocated!");
}

void remove_from_array (uint32 start_address)
{
	for (int i = 0; i < MAX_PAGES; i++)
		{
		// "remove" allocations by nullifying el entries
			if(page_allocations[i].allocation_start == start_address)
			{
				page_allocations[i].allocation_start = 0;
				page_allocations[i].allocation_size = 0;
				break;
			}
		}
}

//=================================
// [1] ALLOCATE SPACE IN USER HEAP:
//=================================
void* malloc(uint32 size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	//==============================================================
	//TODO: [PROJECT'25.IM#2] USER HEAP - #1 malloc
	//Your code is here
	//Comment the following line
	//panic("malloc() is not implemented yet...!!");
#if USE_KHEAP
	uint32 USER_HEAP_MEM_SIZE = USER_HEAP_MAX - USER_HEAP_START;
	uint32 total_size = (uint32)ROUNDUP(size, PAGE_SIZE);

	// check if size is within bounds

	if (size > USER_HEAP_MEM_SIZE)
	{
		return 0;
	}

	// allocate block
	else if (size <= DYN_ALLOC_MAX_BLOCK_SIZE)
	{

		return alloc_block(size);
	}

	else
	{
		//acquire_uspinlock(&uheap_safety_lock);

		// custom fit returns the address to start allocate from
		uint32 start_address = (uint32)custom_fit_search(size, uheapPageAllocStart, uheapPageAllocBreak);

		// if custom fit returned NULL, there's no space to allocate
		if (start_address == 0)
		{
			//release_uspinlock(&uheap_safety_lock);
			return 0;
		}

		// call allocate user mem via sys call
		sys_allocate_user_mem(start_address, total_size);

		//store size to be done
		add_to_array(start_address, total_size);

		//release_uspinlock(&uheap_safety_lock);

	    return (void *)start_address;
	}
#else
		 panic("Kheap disabled!");
#endif
}

//=================================
// [2] FREE SPACE FROM USER HEAP:
//=================================
void free(void* virtual_address)
{
	//TODO: [PROJECT'25.IM#2] USER HEAP - #3 free
	//Your code is here
	//Comment the following line
	//panic("free() is not implemented yet...!!");
#if USE_KHEAP
	uint32 va = (uint32)virtual_address;

		 // check eni within User heap bounds
		 if (va < USER_HEAP_START || va > USER_HEAP_MAX)
		 {
			panic("Address Out of User Heap Bounds!");
		 }

		 // free block
		 else if (va >= dynAllocStart && va < dynAllocEnd){
			 return free_block(virtual_address);
		 }

		 // no pages allocated aslan no need to check for a specific address
		 else
		 {
			 if (allocated_pages == 0)
			 {
					panic("Inserted Address Not Allocated!");
			 }

			 //acquire_uspinlock(&uheap_safety_lock);

			 // make sure ene ba check mn awl el pages (page-aligned)
			 uint32 start_address = (uint32)ROUNDDOWN(virtual_address, PAGE_SIZE);

			 // find size from struct where i store start address and size
			 uint32 size = find_page_size(start_address);

			 // syscall to run kernel side functions
			 sys_free_user_mem(start_address, size);

			 // after kernel frees it from mem, remove from my array
			 remove_from_array(start_address);

			 uint32 pages = size / PAGE_SIZE;
			 uint32 end_address = start_address + size;

			 // check lw this was the last allocation before break
			 // if so, nenazl el break
			 if (end_address == uheapPageAllocBreak)
			 	    {
			 	        uint32 new_break = uheapPageAllocStart;
			 	        for (int i = 0; i < allocated_pages; i++)
			 	        {
			 	        	uint32 allocation_end = page_allocations[i].allocation_start + (page_allocations[i].allocation_size);
			 	        	if (allocation_end > new_break)
			 	        	{
			 	        		new_break = allocation_end;
			 	        	}
			 	        }

			 	        new_break = ROUNDUP(new_break, PAGE_SIZE);

			 	        if (new_break < uheapPageAllocBreak) {
			 		        uheapPageAllocBreak = new_break;
			 	        }
			 	    }

			 //release_uspinlock(&uheap_safety_lock);

		 }
#else
		 panic("Kheap disabled!");
#endif

}

//=================================
// [3] ALLOCATE SHARED VARIABLE:
//=================================
void* smalloc(char *sharedVarName, uint32 size, uint8 isWritable)
{
#if USE_KHEAP

	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	if (size == 0) return NULL ;
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #2 smalloc
	//Your code is here
	//Comment the following line
	//panic("smalloc() is not implemented yet...!!");

	//check for size
	//mynfa3sh a-allocate space akbar mn el shared mem
	uint32 SHARED_MEM_SIZE = USER_HEAP_MAX - uheapPageAllocStart;
	if (size > SHARED_MEM_SIZE){
		return NULL;
	}

	void* start_address = custom_fit_search(size, uheapPageAllocStart, uheapPageAllocBreak);
	int ret = sys_create_shared_object(sharedVarName, size, isWritable,start_address);

	if (ret<0){
	   return NULL;
	}
	return start_address;

#else
	panic("not handled when KERN HEAP is disabled");
#endif

}


//========================================
// [4] SHARE ON ALLOCATED SHARED VARIABLE:
//========================================
void* sget(int32 ownerEnvID, char *sharedVarName)
{
#if USE_KHEAP

	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================

	//TODO: [PROJECT'25.IM#3] SHARED MEMORY - #4 sget
	//Your code is here
	//Comment the following line
	//panic("sget() is not implemented yet...!!");

	int size = sys_size_of_shared_object(ownerEnvID,sharedVarName);
	//checkingg
	//lw mla2ash size el shared_object (msh mwgooda)
	if(size<=0){
		return NULL;
	}

	void* start_address = custom_fit_search(size,uheapPageAllocStart,uheapPageAllocBreak);
	int ret = sys_get_shared_object(ownerEnvID,sharedVarName,start_address);

	if (ret<0){
	  return NULL;
	}
	return start_address;

#else
	panic("not handled when KERN HEAP is disabled");
#endif
}



//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//


//=================================
// REALLOC USER SPACE:
//=================================
//	Attempts to resize the allocated space at "virtual_address" to "new_size" bytes,
//	possibly moving it in the heap.
//	If successful, returns the new virtual_address, in which case the old virtual_address must no longer be accessed.
//	On failure, returns a null pointer, and the old virtual_address remains valid.

//	A call with virtual_address = null is equivalent to malloc().
//	A call with new_size = zero is equivalent to free().

//  Hint: you may need to use the sys_move_user_mem(...)
//		which switches to the kernel mode, calls move_user_mem(...)
//		in "kern/mem/chunk_operations.c", then switch back to the user mode here
//	the move_user_mem() function is empty, make sure to implement it.
void *realloc(void *virtual_address, uint32 new_size)
{
	//==============================================================
	//DON'T CHANGE THIS CODE========================================
	uheap_init();
	//==============================================================
	panic("realloc() is not implemented yet...!!");
}


//=================================
// FREE SHARED VARIABLE:
//=================================
//	This function frees the shared variable at the given virtual_address
//	To do this, we need to switch to the kernel, free the pages AND "EMPTY" PAGE TABLES
//	from main memory then switch back to the user again.
//
//	use sys_delete_shared_object(...); which switches to the kernel mode,
//	calls delete_shared_object(...) in "shared_memory_manager.c", then switch back to the user mode here
//	the delete_shared_object() function is empty, make sure to implement it.
void sfree(void* virtual_address)
{
	//TODO: [PROJECT'25.BONUS#5] EXIT #2 - sfree
	//Your code is here
	//Comment the following line
	panic("sfree() is not implemented yet...!!");

	//	1) you should find the ID of the shared variable at the given address
	//	2) you need to call sys_freeSharedObject()
}


//==================================================================================//
//========================== MODIFICATION FUNCTIONS ================================//
//==================================================================================//
