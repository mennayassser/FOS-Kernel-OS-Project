/*
 * dynamic_allocator.c
 *
 *  Created on: Sep 21, 2023
 *      Author: HP
 */
#include <inc/assert.h>
#include <inc/string.h>
#include "../inc/dynamic_allocator.h"

//==================================================================================//
//============================== GIVEN FUNCTIONS ===================================//
//==================================================================================//
//==================================
//==================================
// [1] GET PAGE VA:
//==================================
__inline__ uint32 to_page_va(struct PageInfoElement *ptrPageInfo)
{
	if (ptrPageInfo < &pageBlockInfoArr[0] || ptrPageInfo >= &pageBlockInfoArr[DYN_ALLOC_MAX_SIZE/PAGE_SIZE])
			panic("to_page_va called with invalid pageInfoPtr");
	//Get start VA of the page from the corresponding Page Info pointer
	int idxInPageInfoArr = (ptrPageInfo - pageBlockInfoArr);
	return dynAllocStart + (idxInPageInfoArr << PGSHIFT);
}

//==================================
// [2] GET PAGE INFO OF PAGE VA:
//==================================
__inline__ struct PageInfoElement * to_page_info(uint32 va)
{
	int idxInPageInfoArr = (va - dynAllocStart) >> PGSHIFT;
	if (idxInPageInfoArr < 0 || idxInPageInfoArr >= DYN_ALLOC_MAX_SIZE/PAGE_SIZE)
		panic("to_page_info called with invalid pa");
	return &pageBlockInfoArr[idxInPageInfoArr];
}

//==================================================================================//
//============================ REQUIRED FUNCTIONS ==================================//
//==================================================================================//

//==================================
// [1] INITIALIZE DYNAMIC ALLOCATOR:
//==================================
bool is_initialized = 0;
void initialize_dynamic_allocator(uint32 daStart, uint32 daEnd)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(daEnd <= daStart + DYN_ALLOC_MAX_SIZE);
		is_initialized = 1;
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #1 initialize_dynamic_allocator
	//Your code is here
	//Comment the following line
	//panic("initialize_dynamic_allocator() Not implemented yet");

	dynAllocStart = daStart;
	dynAllocEnd = daEnd;

	//initialize freeBlockLists
	for (int i = 0; i <=LOG2_MAX_SIZE - LOG2_MIN_SIZE; i++) {
		LIST_INIT(&freeBlockLists[i]);
	}

	//initialize pageBlockInfoArr
	//block size = 0
	//block num = 0
	for(int i=0; i< (daEnd-daStart)/PAGE_SIZE;i++){
		pageBlockInfoArr[i].block_size = 0;
		pageBlockInfoArr[i].num_of_free_blocks = 0;
	}

	//initialize freePagesList
	//all pages are free at first
	LIST_INIT(&freePagesList);
	for (int i=0; i<(daEnd-daStart)/PAGE_SIZE; i++) {
		LIST_INSERT_TAIL(&freePagesList, &pageBlockInfoArr[i]);
	}

}

//===========================
// [2] GET BLOCK SIZE:
//===========================
__inline__ uint32 get_block_size(void *va)
{
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #2 get_block_size
	//Your code is here
	//Comment the following line
	//panic("get_block_size() Not implemented yet");

	uint32 frs_Page_Va = ROUNDDOWN((uint32)va, PAGE_SIZE);
	int frs_page_index = (frs_Page_Va - dynAllocStart) / PAGE_SIZE;
	struct PageInfoElement *frs_pageInfo = &pageBlockInfoArr[frs_page_index];
	uint32 frs_block_size = frs_pageInfo->block_size;
	return frs_block_size;
}

//===========================
// 3) ALLOCATE BLOCK:
//===========================
void *alloc_block(uint32 size)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert(size <= DYN_ALLOC_MAX_BLOCK_SIZE);
	}
	//==================================================================================
	//==================================================================================
	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #3 alloc_block
	//Your code is here
	//Comment the following line
	//panic("alloc_block() Not implemented yet");

	//msh bt-return el log bt-return el bits
	//lazem azawed -1 fel calculations
	inline unsigned int log2_ceil(unsigned int x){
		if (x <= 1) return 1;
		//int power = 2;
		int bits_cnt = 2 ;
		x--;
		while (x >>= 1) {
			//power <<= 1;
			bits_cnt++ ;
		}
		return bits_cnt;

	}

	//a3ml roundup lel size
	uint32 next_pow2(uint32 x){
		uint32 power = 1;
		while(power < x){
			power <<= 1;
		}
		//lw size less than el min akhleeh bel min (8B)
		if(power < DYN_ALLOC_MIN_BLOCK_SIZE){
			power = DYN_ALLOC_MIN_BLOCK_SIZE;
		}
		return power;
	}


	uint32 frda_currentSize = next_pow2(size);
	int frda_freeBlockIndex= log2_ceil(frda_currentSize)-LOG2_MIN_SIZE-1;

	struct BlockElement* blockva = freeBlockLists[frda_freeBlockIndex].lh_first;
	struct PageInfoElement* freePage= LIST_FIRST(&freePagesList);

	//case 1: free blocks are found
	if(blockva != NULL){
		uint32 frda_pageBlockIndex=(ROUNDDOWN((uint32)blockva, PAGE_SIZE) - dynAllocStart) / PAGE_SIZE;
		LIST_REMOVE(&freeBlockLists[frda_freeBlockIndex], blockva);
		pageBlockInfoArr[frda_pageBlockIndex].num_of_free_blocks--;

		return (void*)blockva;
	}

	//case 2: no free blocks but free pages are found
	else if(blockva == NULL && freePage != NULL){
		uint32 va = to_page_va(freePage);
		int ret =get_page((void*)va);
		if (ret != 0) {
			panic("can't allocate page\n");
		}
		//memset((void*)va, 0, PAGE_SIZE);
		LIST_REMOVE(&freePagesList,freePage);
		uint32 frda_pageBlockIndex =(ROUNDDOWN(va, PAGE_SIZE) - dynAllocStart) / PAGE_SIZE;
		pageBlockInfoArr[frda_pageBlockIndex].block_size = frda_currentSize;
		pageBlockInfoArr[frda_pageBlockIndex].num_of_free_blocks= PAGE_SIZE/frda_currentSize;
		for (int i = 0; i < PAGE_SIZE/frda_currentSize; i++) {
			struct BlockElement* block =(struct BlockElement*)((uint32)va + i * frda_currentSize);
			LIST_INSERT_TAIL(&freeBlockLists[frda_freeBlockIndex], block);
		}
		blockva = freeBlockLists[frda_freeBlockIndex].lh_first;
		LIST_REMOVE(&freeBlockLists[frda_freeBlockIndex], blockva);
		pageBlockInfoArr[frda_pageBlockIndex].num_of_free_blocks--;

		return (void*)blockva;
	}

	//case 3: no free blocks and no free pages
	//search for blocks of higher size
	//hyb2a feeh waste of storage bs mafeesh 7al tany
	else if(blockva == NULL && freePage == NULL){
		for(int i = frda_freeBlockIndex+1; i <=LOG2_MAX_SIZE; i++){
			if(freeBlockLists[i].lh_first != NULL){
				blockva = freeBlockLists[i].lh_first;
				LIST_REMOVE(&freeBlockLists[i], blockva);
				uint32 frda_pageBlockIndex =(ROUNDDOWN((uint32)blockva, PAGE_SIZE) - dynAllocStart) / PAGE_SIZE;
				pageBlockInfoArr[frda_pageBlockIndex].num_of_free_blocks--;

				return (void*)blockva;
			}
		}
	 }

	//case 4: panic
	else{
	    panic("can't allocate block\n");
	}

	return NULL;
	//TODO: [PROJECT'25.BONUS#1] DYNAMIC ALLOCATOR - block if no free block
}

//===========================
// [4] FREE BLOCK:
//===========================
void free_block(void *va)
{
	//==================================================================================
	//DON'T CHANGE THESE LINES==========================================================
	//==================================================================================
	{
		assert((uint32)va >= dynAllocStart && (uint32)va < dynAllocEnd);
	}
	//==================================================================================
	//==================================================================================

	//TODO: [PROJECT'25.GM#1] DYNAMIC ALLOCATOR - #4 free_block
	//Your code is here
	//Comment the following line
	//panic("free_block() Not implemented yet");

	inline unsigned int log2_ceil(unsigned int x){
		if (x <= 1) return 1;
		//int power = 2;
		int bits_cnt = 2 ;
		x--;
		while (x >>= 1) {
			//power <<= 1;
			bits_cnt++ ;
		}
		return bits_cnt;

	}

	uint32 block_size = get_block_size(va);

	uint32 freeBlockIndex= log2_ceil(block_size)-LOG2_MIN_SIZE-1;
	struct BlockElement* blockaya = (struct BlockElement*)va;
	LIST_INSERT_TAIL(&freeBlockLists[freeBlockIndex], blockaya);
	uint32 frs_Page_Va = ROUNDDOWN((uint32)va, PAGE_SIZE);
	int frs_page_index = (frs_Page_Va - dynAllocStart) / PAGE_SIZE;
	pageBlockInfoArr[frs_page_index].num_of_free_blocks++;

	//lw kol el block free hkhaly el page free
	int max_num_of_free = PAGE_SIZE/block_size;
	if(pageBlockInfoArr[frs_page_index].num_of_free_blocks==max_num_of_free){
		for(int i=0;i<max_num_of_free;i++){
			struct BlockElement* blockaya = freeBlockLists[freeBlockIndex].lh_first;
			LIST_REMOVE(&freeBlockLists[freeBlockIndex], blockaya);
		}

		struct PageInfoElement* page = &pageBlockInfoArr[frs_page_index];
		LIST_INSERT_TAIL(&freePagesList, page);
		pageBlockInfoArr[frs_page_index].block_size=0;
		pageBlockInfoArr[frs_page_index].num_of_free_blocks=0;

		return_page((void*)frs_Page_Va);
   }
}

//==================================================================================//
//============================== BONUS FUNCTIONS ===================================//
//==================================================================================//

//===========================
// [1] REALLOCATE BLOCK:
//===========================
void *realloc_block(void* va, uint32 ms)
{
	uint32 ma = (uint32)va;

	if (ma == 0) {
		return alloc_block(ms);
	}

	if (ms == 0) {
		free_block((void*)ma);
		return (void*)ma;
	}

	if (ms < 8) ms = 8;

	uint32* mh = (uint32*)(ma - 4);
	uint32 mi = *mh;
	uint32 ms_old = mi & 0xFFFFFFFE;
	uint32 md_old = ms_old - 4;

	if (mi & 1) {
		*mh = ms_old;
	}

	uint32 mt = 4 + ms;

	if (mt == ms_old) return NULL;

	if (mt < ms_old) {
		uint32 me = ms_old - mt;

		if (me >= 8) {
			*mh = mt;

			uint32 mf = ma + ms;
			uint32* mfh = (uint32*)mf;
			*mfh = me | 1;
		}
		return NULL;
	}

	uint32 mn = ma + ms_old;

	if (mn < 0xC0000000) {
		uint32* mnh = (uint32*)(mn - 4);
		uint32 mni = *mnh;

		if (mni & 1) {
			uint32 mns = mni & 0xFFFFFFFE;
			uint32 mc = ms_old + mns;

			if (mc >= mt) {
				*mh = mc;

				if (mc > mt) {
					uint32 ml = mc - mt;
					if (ml >= 8) {
						uint32 mlp = ma + ms;
						uint32* mlh = (uint32*)mlp;
						*mlh = ml | 1;
					}
				}
				return NULL;
			}
		}
	}

	uint32* mtb = (uint32 *)alloc_block(md_old);
	if (!mtb) return NULL;

	memcpy(mtb, (void*)ma, md_old);

	free_block((void*)ma);

	void* mnb = (void *)alloc_block(ms);
	if (mnb) {
		uint32 mc = (md_old < ms) ? md_old : ms;
		memcpy(mnb, mtb, mc);
	}

	free_block(mtb);

	return NULL;
}
