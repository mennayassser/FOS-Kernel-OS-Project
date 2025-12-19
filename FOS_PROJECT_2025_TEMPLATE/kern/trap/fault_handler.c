/*
 * fault_handler.c
 *
 *  Created on: Oct 12, 2022
 *      Author: HP
 */

#include "trap.h"
#include <kern/proc/user_environment.h>
#include <kern/cpu/sched.h>
#include <kern/cpu/cpu.h>
#include <kern/disk/pagefile_manager.h>
#include <kern/mem/memory_manager.h>
#include <kern/mem/kheap.h>
#include "kern/mem/working_set_manager.h"



//2014 Test Free(): Set it to bypass the PAGE FAULT on an instruction with this length and continue executing the next one
// 0 means don't bypass the PAGE FAULT
uint8 bypassInstrLength = 0;

//===============================
// REPLACEMENT STRATEGIES
//===============================
//2020
void setPageReplacmentAlgorithmLRU(int LRU_TYPE)
{
	assert(LRU_TYPE == PG_REP_LRU_TIME_APPROX || LRU_TYPE == PG_REP_LRU_LISTS_APPROX);
	_PageRepAlgoType = LRU_TYPE ;
}
void setPageReplacmentAlgorithmCLOCK(){_PageRepAlgoType = PG_REP_CLOCK;}
void setPageReplacmentAlgorithmFIFO(){_PageRepAlgoType = PG_REP_FIFO;}
void setPageReplacmentAlgorithmModifiedCLOCK(){_PageRepAlgoType = PG_REP_MODIFIEDCLOCK;}
/*2018*/ void setPageReplacmentAlgorithmDynamicLocal(){_PageRepAlgoType = PG_REP_DYNAMIC_LOCAL;}
/*2021*/ void setPageReplacmentAlgorithmNchanceCLOCK(int PageWSMaxSweeps){_PageRepAlgoType = PG_REP_NchanceCLOCK;  page_WS_max_sweeps = PageWSMaxSweeps;}
/*2024*/ void setFASTNchanceCLOCK(bool fast){ FASTNchanceCLOCK = fast; };
/*2025*/ void setPageReplacmentAlgorithmOPTIMAL(){ _PageRepAlgoType = PG_REP_OPTIMAL; };

//2020
uint32 isPageReplacmentAlgorithmLRU(int LRU_TYPE){return _PageRepAlgoType == LRU_TYPE ? 1 : 0;}
uint32 isPageReplacmentAlgorithmCLOCK(){if(_PageRepAlgoType == PG_REP_CLOCK) return 1; return 0;}
uint32 isPageReplacmentAlgorithmFIFO(){if(_PageRepAlgoType == PG_REP_FIFO) return 1; return 0;}
uint32 isPageReplacmentAlgorithmModifiedCLOCK(){if(_PageRepAlgoType == PG_REP_MODIFIEDCLOCK) return 1; return 0;}
/*2018*/ uint32 isPageReplacmentAlgorithmDynamicLocal(){if(_PageRepAlgoType == PG_REP_DYNAMIC_LOCAL) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmNchanceCLOCK(){if(_PageRepAlgoType == PG_REP_NchanceCLOCK) return 1; return 0;}
/*2021*/ uint32 isPageReplacmentAlgorithmOPTIMAL(){if(_PageRepAlgoType == PG_REP_OPTIMAL) return 1; return 0;}

//===============================
// PAGE BUFFERING
//===============================
void enableModifiedBuffer(uint32 enableIt){_EnableModifiedBuffer = enableIt;}
uint8 isModifiedBufferEnabled(){  return _EnableModifiedBuffer ; }

void enableBuffering(uint32 enableIt){_EnableBuffering = enableIt;}
uint8 isBufferingEnabled(){  return _EnableBuffering ; }

void setModifiedBufferLength(uint32 length) { _ModifiedBufferLength = length;}
uint32 getModifiedBufferLength() { return _ModifiedBufferLength;}

//===============================
// FAULT HANDLERS
//===============================

//==================
// [0] INIT HANDLER:
//==================
void fault_handler_init()
{
	//setPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX);
	//setPageReplacmentAlgorithmOPTIMAL();
	setPageReplacmentAlgorithmCLOCK();
	//setPageReplacmentAlgorithmModifiedCLOCK();
	enableBuffering(0);
	enableModifiedBuffer(0) ;
	setModifiedBufferLength(1000);
}
//==================
// [1] MAIN HANDLER:
//==================
/*2022*/
uint32 last_eip = 0;
uint32 before_last_eip = 0;
uint32 last_fault_va = 0;
uint32 before_last_fault_va = 0;
int8 num_repeated_fault  = 0;
extern uint32 sys_calculate_free_frames() ;

struct Env* last_faulted_env = NULL;
//extern __inline__ uint32 get_block_size(void *va);
void fault_handler(struct Trapframe *tf)
{

	// Read processor's CR2 register to find the faulting address
		uint32 fault_va = rcr2();
		//	cprintf("\n*****Faulted VA = %x*****\n", fault_va);
		//	print_trapframe(tf);
		/******************/

		//If same fault va for 3 times, then panic
		//UPDATE: 3 FAULTS MUST come from the same environment (or the kernel)
		struct Env* cur_env = get_cpu_proc();
		//cprintf("cur envvvvv" , cur_env);
		if (last_fault_va == fault_va && last_faulted_env == cur_env)
		{
			num_repeated_fault++ ;
			if (num_repeated_fault == 3)
			{
				print_trapframe(tf);
				panic("Failed to handle fault! fault @ at va = %x from eip = %x causes va (%x) to be faulted for 3 successive times\n", before_last_fault_va, before_last_eip, fault_va);
			}
		}
		else
		{
			before_last_fault_va = last_fault_va;
			before_last_eip = last_eip;
			num_repeated_fault = 0;
		}
		last_eip = (uint32)tf->tf_eip;
		last_fault_va = fault_va ;
		last_faulted_env = cur_env;
		/******************/
		//2017: Check stack overflow for Kernel
		int userTrap = 0;
		if ((tf->tf_cs & 3) == 3) {
			userTrap = 1;
		}
		if (!userTrap)
		{
			struct cpu* c = mycpu();
			//cprintf("trap from KERNEL\n");
			if (cur_env && fault_va >= (uint32)cur_env->kstack && fault_va < (uint32)cur_env->kstack + PAGE_SIZE)
				panic("User Kernel Stack: overflow exception!");
			else if (fault_va >= (uint32)c->stack && fault_va < (uint32)c->stack + PAGE_SIZE)
				panic("Sched Kernel Stack of CPU #%d: overflow exception!", c - CPUS);
	#if USE_KHEAP
			if (fault_va >= KERNEL_HEAP_MAX)
				panic("Kernel: heap overflow exception!");
	#endif
		}
		//2017: Check stack underflow for User
		else
		{
			//cprintf("trap from USER\n");
			if (fault_va >= USTACKTOP && fault_va < USER_TOP)
				panic("User: stack underflow exception!");
		}

		//get a pointer to the environment that caused the fault at runtime
		//cprintf("curenv = %x\n", curenv);
		struct Env* faulted_env = cur_env;
		if (faulted_env == NULL)
		{
			//cprintf("faulted_env=",(void*)cur_env);
			//print_trapframe(tf);
			panic("faulted env == NULL!");
		}
		//check the faulted address, is it a table or not ?
		//If the directory entry of the faulted address is NOT PRESENT then
		if ( (faulted_env->env_page_directory[PDX(fault_va)] & PERM_PRESENT) != PERM_PRESENT)
		{
			// we have a table fault =============================================================
			//		cprintf("[%s] user TABLE fault va %08x\n", curenv->prog_name, fault_va);
			//		print_trapframe(tf);

			faulted_env->tableFaultsCounter ++ ;

			table_fault_handler(faulted_env, fault_va);
		}
		else
		{

				//FAULT HANDLER - Check for invalid pointers
				//(e.g. pointing to unmarked user heap page, kernel or wrong access rights),
				//your code is here

			if (userTrap)
			{
				//if(faulted_env==NULL){panic("ENV PANIC");}
				uint32 pde = faulted_env->env_page_directory[PDX(fault_va)];
				uint32 perm = 0;

				if (pde & PERM_PRESENT) {
				    uint32 *page_table = NULL; //CALL FUNCTION GET PAGE TABLE
				    get_page_table(faulted_env->env_page_directory,fault_va,&page_table);
				    perm = page_table[PTX(fault_va)] & 0xFFF;  //get the perms
				}

		                  /*if(fault_va>=USER_TOP){
							env_exit();
							return;
							}*/
			            //user heap
			           //writeable
			          //present and not kernel should user =1
				      if(fault_va>=USER_HEAP_START && fault_va<USER_HEAP_MAX && (perm & PERM_UHPAGE)==0 ){
				    	  //i should check if it's in user heap or notttt
				    	  cprintf("a");
					 		env_exit();
							return;

						}
				      if (perm & PERM_PRESENT) {
				       if (!(perm & PERM_WRITEABLE)) {   //if it's read only
							env_exit();
				             return;

						}
				      }
				      if (perm & PERM_PRESENT) {
				      	if (!(perm & PERM_USER)) {     //if it's kernel cause user = 0
				      	      env_exit();
				      	      return;

				      						}
				      				      }


			}

			/*2022: Check if fault due to Access Rights */
			//uint32 perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
			//int perms = pt_get_page_permissions(faulted_env->env_page_directory, fault_va);
			//if (perms & PERM_PRESENT)
				//panic("Page @va=%x is exist! page fault due to violation of ACCESS RIGHTS\n", fault_va) ;



			// we have normal page fault =============================================================
			faulted_env->pageFaultsCounter ++ ;

			//		cprintf("[%08s] user PAGE fault va %08x\n", curenv->prog_name, fault_va);
			//		cprintf("\nPage working set BEFORE fault handler...\n");
			//		env_page_ws_print(curenv);

			if(isBufferingEnabled())
			{
				__page_fault_handler_with_buffering(faulted_env, fault_va);
			}
			else
			{
				//page_fault_handler(faulted_env, fault_va);
				page_fault_handler(faulted_env, fault_va);
			}
			//		cprintf("\nPage working set AFTER fault handler...\n");
			//		env_page_ws_print(curenv);


		}

}
//=========================
// [2] TABLE FAULT HANDLER:
//=========================
void table_fault_handler(struct Env * curenv, uint32 fault_va)
{
	//panic("table_fault_handler() is not implemented yet...!!");
	//Check if it's a stack page
	uint32* ptr_table;
#if USE_KHEAP
	{
		ptr_table = create_page_table(curenv->env_page_directory, (uint32)fault_va);
	}
#else
	{
		__static_cpt(curenv->env_page_directory, (uint32)fault_va, &ptr_table);
	}
#endif
}

//=========================
// [3] PAGE FAULT HANDLER:
//=========================
/* Calculate the number of page faults according th the OPTIMAL replacement strategy
 * Given:
 * 	1. Initial Working Set List (that the process started with)
 * 	2. Max Working Set Size
 * 	3. Page References List (contains the stream of referenced VAs till the process finished)
 *
 * 	IMPORTANT: This function SHOULD NOT change any of the given lists
 */
int get_optimal_num_faults(struct WS_List *initWorkingSet, int maxWSSize, struct PageRef_List *pageReferences)
{
	//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #2 get_optimal_num_faults
	//Your code is here
	//Comment the following line
	int numoffaultss=0;
		struct WS_List copy_working_set;
		LIST_INIT(&copy_working_set);

		// should Copy initial working set cause i can't change it
		struct WorkingSetElement* COPY = LIST_FIRST(initWorkingSet);
		while(COPY != NULL)
		{
			struct WorkingSetElement*new_set=kmalloc(sizeof(struct WorkingSetElement));
			new_set->virtual_address=COPY->virtual_address;
			LIST_INSERT_TAIL(&copy_working_set, new_set);
			COPY = LIST_NEXT(COPY);
		}

		struct PageRefElement* ref_elem = LIST_FIRST(pageReferences);
        /////////should refer  to the first element in the reff streammmmm
		while(ref_elem != NULL)
		{
			// check  page exists in working set or not
			int found = 0;
			struct WorkingSetElement * ws_elem = LIST_FIRST(&copy_working_set);
			while(ws_elem != NULL)
			{
				if (ws_elem->virtual_address == ref_elem->virtual_address)
				{
					found = 1;
					break;
				}
				ws_elem = LIST_NEXT(ws_elem);
			}

			//if i didn't find it in ws list then increment the num of faultss
			if(found != 1){
				numoffaultss++;

				//should check if there's a place to insert in WS or not
				if(LIST_SIZE(&copy_working_set) < maxWSSize){
					struct WorkingSetElement *newElem = kmalloc(sizeof(struct WorkingSetElement));
					newElem->virtual_address = ref_elem->virtual_address;
					LIST_INSERT_TAIL(&copy_working_set, newElem);
				}
				else{
					//no place should delete
					struct WorkingSetElement *victim = NULL;
					int max_distancee = -1;

					struct WorkingSetElement* ws = LIST_FIRST(&copy_working_set);
					while(ws != NULL)
					{
						int distance = 0;
						int willuse = 0;

						// search in  references
						struct PageRefElement* searchh_ref = LIST_NEXT(ref_elem);
						while(searchh_ref != NULL)
						{
							distance++;
							if(searchh_ref->virtual_address == ws->virtual_address)
							{
								willuse = 1; //mawgoda felrefrence stream hstakhdemha taniii
								break;
							}
							searchh_ref = LIST_NEXT(searchh_ref);
						}

						//
						if(!willuse)
						{
							victim = ws;
							break;
						}

						// remove the furthest page
						if(distance > max_distancee)
						{
							max_distancee = distance; //hashel eli 3ando max distance w ha update elmax
							victim = ws;
						}

						ws = LIST_NEXT(ws);
					}

					// ashel elvictim and add the page
					if(victim != NULL)
					{
						LIST_REMOVE(&copy_working_set, victim);
						kfree(victim);
					}
                    //adding in the copy working set not in the original one
					struct WorkingSetElement *newElem = kmalloc(sizeof(struct WorkingSetElement));
					newElem->virtual_address = ref_elem->virtual_address;
					LIST_INSERT_TAIL(&copy_working_set, newElem);
				}
			}

			ref_elem = LIST_NEXT(ref_elem);
		}

		/////////////wanna delete the copy working set
		struct WorkingSetElement* tmp = LIST_FIRST(&copy_working_set);
		while(tmp != NULL)
		{
			struct WorkingSetElement* nextt = LIST_NEXT(tmp);
			LIST_REMOVE(&copy_working_set, tmp);
			kfree(tmp);
			tmp = nextt;
		}
        //////////functiom int lazem traga3 7aga
		return numoffaultss;

	//panic("get_optimal_num_faults() is not implemented yet...!!");
}


void page_fault_handler(struct Env * faulted_env, uint32 fault_va)
{

	struct FrameInfo *ptr_frame_info;
	    uint32 virtualaddress = ROUNDDOWN(fault_va, PAGE_SIZE);



	    #if USE_KHEAP
	    if (isPageReplacmentAlgorithmOPTIMAL())
	    	        {
	    	            //TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #1 Optimal Reference Stream
	    	        	struct PageRefElement*ref_elem=kmalloc(sizeof(struct PageRefElement));

	    	        	ref_elem->virtual_address=virtualaddress;
	    	        	//alloc_page(faulted_env->env_page_directory, virtualaddress, PERM_PRESENT | PERM_WRITEABLE | PERM_USER, 0);
	    	        	LIST_INSERT_TAIL(&faulted_env->referenceStreamList, ref_elem);
                        //creating refrence streammmmmmmmm




	    	        	uint32*page_table=NULL;
	    	        	get_page_table(faulted_env->env_page_directory,virtualaddress,&page_table);

	    	        	int ret=page_table[PTX(virtualaddress)]; //lazem ashof elentry mawgoda wla la
	    	        	if( ret==0){
	    	        		alloc_page(faulted_env->env_page_directory, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER, 0);
                            //badal elmap w elallocate frame

	    	        		pf_read_env_page(faulted_env, (void*)fault_va);

	    	        		   }
	    	        	else{

	    	        		pt_set_page_permissions(faulted_env->env_page_directory, virtualaddress, PERM_PRESENT,0 );
                            //set present to 1

	    	        		}


	    	        	struct WorkingSetElement *New_element = env_page_ws_list_create_element(faulted_env, virtualaddress);

	    	        	if(LIST_SIZE(&(faulted_env->new_WS_list))<(faulted_env->page_WS_max_size)){

	    	        		LIST_INSERT_TAIL(&(faulted_env->new_WS_list), New_element);
                             //inserting the new element cause it's not full yet



	    	        	}
	    	        	else{


	    	        struct WorkingSetElement* copy=NULL;


	    			LIST_FOREACH_SAFE(copy, &faulted_env->new_WS_list, WorkingSetElement)
	    			{
	    				pt_set_page_permissions(faulted_env->env_page_directory, copy->virtual_address, 0,PERM_PRESENT );
	    				LIST_REMOVE(&faulted_env->new_WS_list,copy);

	    				kfree(copy);
                        //set all the present to zero and deleting all the element in the set

	    			}
	    			//struct WorkingSetElement *New_element = env_page_ws_list_create_element(faulted_env, virtualaddress);
	    			LIST_INSERT_TAIL(&(faulted_env->new_WS_list), New_element);



	    	        }
	    	        	//struct PageRefElement*ref;









	    	            //panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
	    	        }
	        else
	        {
	            struct WorkingSetElement *victimWSElement = NULL;
	            uint32 wsSize = LIST_SIZE(&(faulted_env->page_WS_list));

	            // Check if WS has space
	            if(wsSize < (faulted_env->page_WS_max_size)){
	            	//cprintf("Page fault: placement, WS size %d/%d\n");
	            	    //TODO: [PROJECT'25.GM#3] FAULT HANDLER I - #3 placement
	            	    //Your code is here
	            		//Comment the following line
	            	    ptr_frame_info=NULL;   //should be initialized to null
	            	   	uint32 locate = allocate_frame(&ptr_frame_info);

	            	    if(locate != 0){
	                   panic("page_fault_handler: no memory available");

	             		    }
                        map_frame(faulted_env->env_page_directory,ptr_frame_info,virtualaddress,PERM_USER|PERM_WRITEABLE);
	                    int exist = pf_read_env_page(faulted_env, (void*)virtualaddress); // return E_PAGE_NOT_EXIST_IN_PF

	                    int isheap = (virtualaddress >= USER_HEAP_START &&
	             	            virtualaddress < USER_HEAP_MAX); //heap range
	                    int isstack = (virtualaddress >= USTACKBOTTOM &&virtualaddress < USTACKTOP); //stack range
	                    //check if it's in page file or not
	                    if(exist == E_PAGE_NOT_EXIST_IN_PF)
	                    {
	                    if(isheap || isstack) {
	                    	//cprintf("in heap or stack");
	                    	//i should continue cause it's valid
	                    }
	                    else {
	                        env_exit();
	                        return;
	                        //not in valid region
	                         }
	                         }


	            	     struct WorkingSetElement *ws_element = env_page_ws_list_create_element(faulted_env, virtualaddress);
	            	     //creating an element

	            	     LIST_INSERT_TAIL(&(faulted_env->page_WS_list), ws_element);
	            	     //put the element at the end of the list
	            	     if(LIST_SIZE(&(faulted_env->page_WS_list))==(faulted_env->page_WS_max_size)){
	            	    	 faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
	            	    	 //cause it is full
	            	        }
	            	     else{
	            	    	 faulted_env->page_last_WS_element = NULL;
	            	    	 //it's not full yet

	            	     }



	            	         //cprintf("Page fault: placement, WS size %d/%d\n", new_wssize, faulted_env->page_WS_max_size);
	            }
		else
		{
			if (isPageReplacmentAlgorithmCLOCK())
			{
				//TODO: [PROJECT'25.IM#1] FAULT HANDLER II - #3 Clock Replacement
				//Your code is here
				//Comment the following line

				        // Start from last WS element (for Clock)
				 //panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");

				struct WorkingSetElement * victim = faulted_env->page_last_WS_element;

								uint32* ptr_page_table = NULL;


								uint32 prem = 0; //should declare it



								while(1) //while true
								{
									//should break only if i inserted used=0

									get_page_table(faulted_env->env_page_directory, victim->virtual_address, &ptr_page_table);
									prem = ptr_page_table[PTX(victim->virtual_address)] & 0xfff;
                                    //get the perm every time

									if(prem & PERM_USED)
									{
                                        //if used=1
										pt_set_page_permissions(faulted_env->env_page_directory, victim->virtual_address, 0, PERM_USED);
										//set it to zero
										victim = LIST_NEXT(victim);
										if(!victim)
										{
											//if the victim = null go back
											victim = LIST_FIRST(&(faulted_env->page_WS_list));
										}
									}
									else
									{    //is it's already zero i should stop
										break;
									}

								}

								faulted_env->page_last_WS_element = victim->prev_next_info.le_next;
								if (!faulted_env->page_last_WS_element)
								{
									//nafs el7aga if it's null go back
									faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list));
								}


								if(prem & PERM_MODIFIED)
								{


									struct FrameInfo * frame_info = to_frame_info(EXTRACT_ADDRESS (ptr_page_table[PTX(victim->virtual_address)]));
									///////////should update the page

									pf_update_env_page(faulted_env, victim->virtual_address, frame_info);
								}


								alloc_page(faulted_env->env_page_directory, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER, 0);
                                //lazem a7gez makan lehaaaaaaaaaaa

								pf_read_env_page(faulted_env, (void*)fault_va);



                                 // TACK CARE FIFOOOOOOOOOOOOOOOOOOO

								struct WorkingSetElement * ptr = NULL;
								LIST_FOREACH_SAFE(ptr, &faulted_env->page_WS_list, WorkingSetElement)
								{
									//searching for the victim to remove
									if(ptr != victim)
									{
										//ashelo mn makano w a7oto felakher
										LIST_REMOVE(&faulted_env->page_WS_list,ptr);
										LIST_INSERT_TAIL(&faulted_env->page_WS_list, ptr);
									}
									else
									{
										////////////found the victim
										break;
									}

								}



                                 //remove the victim and inserting the elem tack care
								struct WorkingSetElement * wse = env_page_ws_list_create_element(faulted_env, fault_va);
                                //should insert in tail
								LIST_INSERT_TAIL(&faulted_env->page_WS_list , wse);
								LIST_REMOVE(&faulted_env->page_WS_list, victim);

								unmap_frame(faulted_env->env_page_directory, victim->virtual_address);


								kfree(victim);////////////you forgot to delete

			}

			else if (isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
						{
							//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #2 LRU Aging Replacement
							//Your code is here
							//Comment the following line
							//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
							if(wsSize < (faulted_env->page_WS_max_size))
							{
								//placement
								 ptr_frame_info=NULL;   //should be initialized to null
							   	uint32 locate = allocate_frame(&ptr_frame_info);

								if(locate != 0){
								panic("page_fault_handler: no memory available");

								}
								map_frame(faulted_env->env_page_directory,ptr_frame_info,virtualaddress,PERM_USER|PERM_WRITEABLE);
								int exist = pf_read_env_page(faulted_env, (void*)virtualaddress); // return E_PAGE_NOT_EXIST_IN_PF

								int isheap = (virtualaddress >= USER_HEAP_START &&
								virtualaddress < USER_HEAP_MAX); //heap range
								int isstack = (virtualaddress >= USTACKBOTTOM &&virtualaddress < USTACKTOP); //stack range
								if(exist == E_PAGE_NOT_EXIST_IN_PF)
								{
								 if(isheap || isstack) {
								 //cprintf("in heap or stack");
								 //i should continue cause it's valid
								 }
							     else {
									 env_exit();
									 return;
									//not in valid region
								   }
								 }


								struct WorkingSetElement *ws_element = env_page_ws_list_create_element(faulted_env, virtualaddress); //creating an element

								LIST_INSERT_TAIL(&(faulted_env->page_WS_list), ws_element); //put the element at the end of the list
								if(LIST_SIZE(&(faulted_env->page_WS_list))==(faulted_env->page_WS_max_size)){
							      faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list)); //cause it is full
								 }
								else{
								  faulted_env->page_last_WS_element = NULL; //it's not full yet

								}
							}
							else{
								//replacement using LRU approx algo

								 struct WorkingSetElement *ws_current_element = NULL;
								 struct WorkingSetElement *ws_victim_element = NULL;
								 int max_leading_zeros = -1;

								LIST_FOREACH(ws_current_element, &(faulted_env->page_WS_list))
								{

									uint32 Age = ws_current_element->time_stamp;
									int leading_zeros = 0;

									if(Age == 0) //newest element in ws (time stamp/age = 0)
									{
										leading_zeros = 32;
									}
									else{

										//loop on bits w n3ed leading zeros of current element's age le7ad ma ne5bat fe 1
										for(int i=0 ; i<32 ; i++)
										{
											//negeb a5er bit mn 3ala el shemal
											int left_msb = (Age >> 31) & 1;

											//law 1 break
											if(left_msb == 1)
											{
												break;
											}
											else
											{   //law 0 nezawed el leading_zeros
												leading_zeros++;
												//n7arak el age bit shemal to check next bit
												Age <<= 1;
											}
										}
									}

									//law el leading zeros beta3 el current > max
									if(leading_zeros > max_leading_zeros)
									{   //update el max w el victim
										max_leading_zeros = leading_zeros;
										ws_victim_element = ws_current_element;
									}
								}

								//ba3d ma n loop 3ala el ws kolaha hykon ma3ana el victim
								if(ws_victim_element == NULL)
								 {
								   panic("No victim found in WS using LRU approx");
								}
								else
								{
						          //replacement
									uint32 *victim_page_table = NULL;
									get_page_table(faulted_env->env_page_directory , ws_victim_element->virtual_address , &victim_page_table);
									uint32 table_entry = victim_page_table[PTX(ws_victim_element->virtual_address)];
									int modify_bit = (table_entry & PERM_MODIFIED) ? 1 : 0 ;

									uint32 *frame_ptr;
									struct FrameInfo* victim_frame_info = get_frame_info(faulted_env->env_page_directory, ws_victim_element->virtual_address, &frame_ptr);
                                    //law modified yob2a nektbha fe el disk
									if(modify_bit)
									{
										pf_update_env_page(faulted_env, ws_victim_element->virtual_address, victim_frame_info);
									}
                                    //remove el victim mn el ws
									env_page_ws_invalidate(faulted_env,ws_victim_element->virtual_address);

									struct FrameInfo* new_frame = NULL;
									 uint32 alloc_result = allocate_frame(&new_frame);
									        if(alloc_result != 0) {
									            panic("Cannot allocate frame for replacement");
									        }

									uint32 perm = PERM_USER | PERM_WRITEABLE | PERM_PRESENT;
									if(fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX)
									{
										perm |= PERM_UHPAGE;
									}
									//ba3d ma n allocate new frame w n check 3ala el permissions han map ba2a el frame
									map_frame(faulted_env->env_page_directory, new_frame,virtualaddress, perm);

                                    //check law exists fe el disk wala la2
									int exist = pf_read_env_page(faulted_env, (void*)virtualaddress);

									int isheap = (fault_va >= USER_HEAP_START && fault_va < USER_HEAP_MAX);
									int isstack = (fault_va >= USTACKBOTTOM &&fault_va < USTACKTOP);
									if(exist == E_PAGE_NOT_EXIST_IN_PF)
									{
									  if(isheap || isstack) {
									  //cprintf("in heap or stack");
									  //i should continue cause it's valid
									   }
									  else {
										  unmap_frame(faulted_env->env_page_directory,virtualaddress);
										  env_exit();
										   return;
										   //not in valid region
										 }
								    }

									//insert el new element lel new page fe el ws (tail)
									struct WorkingSetElement * wse = env_page_ws_list_create_element(faulted_env, virtualaddress);
									LIST_INSERT_TAIL(&(faulted_env->page_WS_list), wse);

								}



							}


						}
			else if (isPageReplacmentAlgorithmModifiedCLOCK())
						{
							//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #3 Modified Clock Replacement
							//Your code is here
							//Comment the following line
							//panic("page_fault_handler().REPLACEMENT is not implemented yet...!!");
							if(wsSize < (faulted_env->page_WS_max_size))
							{
								//placement
													 ptr_frame_info=NULL;   //should be initialized to null
												   	uint32 locate = allocate_frame(&ptr_frame_info);

													if(locate != 0){
													panic("page_fault_handler: no memory available");

													}
													map_frame(faulted_env->env_page_directory,ptr_frame_info,virtualaddress,PERM_USER|PERM_WRITEABLE);
													int exist = pf_read_env_page(faulted_env, (void*)virtualaddress); // return E_PAGE_NOT_EXIST_IN_PF

													int isheap = (virtualaddress >= USER_HEAP_START &&
													virtualaddress < USER_HEAP_MAX); //heap range
													int isstack = (virtualaddress >= USTACKBOTTOM &&virtualaddress < USTACKTOP); //stack range
													if(exist == E_PAGE_NOT_EXIST_IN_PF)
													{
													 if(isheap || isstack) {
													 //cprintf("in heap or stack");
													 //i should continue cause it's valid
													 }
												     else {
														 env_exit();
														 return;
														//not in valid region
													   }
													 }


													struct WorkingSetElement *ws_element = env_page_ws_list_create_element(faulted_env, virtualaddress); //creating an element

													LIST_INSERT_TAIL(&(faulted_env->page_WS_list), ws_element); //put the element at the end of the list
													if(LIST_SIZE(&(faulted_env->page_WS_list))==(faulted_env->page_WS_max_size)){
												      faulted_env->page_last_WS_element = LIST_FIRST(&(faulted_env->page_WS_list)); //cause it is full
													 }
													else{
													  faulted_env->page_last_WS_element = NULL; //it's not full yet

													}
							}
							else
							{
								 struct WorkingSetElement *ws_current_element = NULL;
								 struct WorkingSetElement *ws_victim_element = NULL;

								 //han loop 3ala el working set maximun 4 rounds as by the 4th round hykon kol el elemnts el used bit bta3hom ba2a 0 3ala el 2a2l mara
								 int max_rounds = 4;
								 for (int round = 0; round < max_rounds && ws_victim_element == NULL; ++round)
								     {
									     //bdayt el search
								         struct WorkingSetElement *start_element = (faulted_env->page_last_WS_element) ?
								                                                    faulted_env->page_last_WS_element :
								                                                    LIST_FIRST(&(faulted_env->page_WS_list));
								         struct WorkingSetElement *current = start_element;

								         //try1: han loop 3ala kol el elements fe el ws w ndawer 3ala (used_bit = 0 & modified_bit = 0)
								         for (uint32 i = 0; i < wsSize; ++i)
								         {
								             uint32 va = current->virtual_address;
								             uint32 *page_table = NULL;
								             get_page_table(faulted_env->env_page_directory, va, &page_table);
								             uint32 table_entry = page_table[PTX(va)];
								             int used_bit = (table_entry & PERM_USED) ? 1 : 0;
								             int mod_bit = (table_entry & PERM_MODIFIED) ? 1 : 0;

								             //law la2enah yob2a da el victim bta3na w n break
								             if (!used_bit && !mod_bit) {
								                 ws_victim_element = current;
								                 break;
								             }

								             //law la2 nkaml 3ala el elemnt eli ba3do
								             current = LIST_NEXT(current);
								             if (current == NULL)
								                 current = LIST_FIRST(&faulted_env->page_WS_list);
								         }

								         //ba3d try1 law la2ena victim n break law lesa ned5ol 3ala try2
								         if (ws_victim_element != NULL) {
								             break;
								         }

								         //try2: han loop 3ala kol el elements fe el ws w ndawer 3ala el (used_bit = 0)
								         current = start_element;
								         for (uint32 i = 0; i < wsSize; ++i)
								         {
								             uint32 va = current->virtual_address;
								             uint32 *page_table = NULL;
								             get_page_table(faulted_env->env_page_directory, va, &page_table);
								             uint32 table_entry = page_table[PTX(va)];
								             int used_bit = (table_entry & PERM_USED) ? 1 : 0;

								             //law la2enah yob2a da el victim w n break
								             if (!used_bit) {
								                 ws_victim_element = current;
								                 break;
								             }
								             else { // law la2 yob2a n set el used_bit be zero w nro lel element eli ba3do
								                 pt_set_page_permissions(faulted_env->env_page_directory, va, 0, PERM_USED);
								             }

								             current = LIST_NEXT(current);
								             if (current == NULL)
								                 current = LIST_FIRST(&faulted_env->page_WS_list);
								         }

								         //law lesa fe el loop yob2a mala2nahosh fe el round di fa nrga3 ne3ed try1
								     }


								     if (ws_victim_element == NULL) {
								         panic("couldn't find victim using modified clock algo");
								     }

								     //ba3d ma la2ena el victim n update page_last_WS_element lel element ba3d elvictim
								     faulted_env->page_last_WS_element = LIST_NEXT(ws_victim_element);
								     if (faulted_env->page_last_WS_element == NULL)
								         faulted_env->page_last_WS_element = LIST_FIRST(&faulted_env->page_WS_list);

								     // Found victim fa aplly replacement
								     uint32 *victim_page_table = NULL;
								     get_page_table(faulted_env->env_page_directory , ws_victim_element->virtual_address , &victim_page_table);
								     uint32 table_entry = victim_page_table[PTX(ws_victim_element->virtual_address)];
								     int modify_bit = (table_entry & PERM_MODIFIED) ? 1 : 0 ;

								     uint32 *frame_ptr;
								     struct FrameInfo* victim_frame_info = get_frame_info(faulted_env->env_page_directory, ws_victim_element->virtual_address, &frame_ptr);

								     //law modified yob2a nektbha fe el disk
								     if(modify_bit)
								     {
								     	pf_update_env_page(faulted_env, ws_victim_element->virtual_address, victim_frame_info);
								     }

								     //allocte new frame w map it
								     alloc_page(faulted_env->env_page_directory, fault_va, PERM_PRESENT | PERM_WRITEABLE | PERM_USER, 0);

                                     //read page mn el disk
								     pf_read_env_page(faulted_env, (void*)fault_va);

                                     //loop 3ala ws w y7ark kol el pages lel tail w yo2f 3and le victim (to maintain fifo)
                                     struct WorkingSetElement * ptr = NULL;
								     LIST_FOREACH_SAFE(ptr, &faulted_env->page_WS_list, WorkingSetElement)
								     	{
								     		if(ptr != ws_victim_element)
								     		{
								     		  LIST_REMOVE(&faulted_env->page_WS_list,ptr);
								     		  LIST_INSERT_TAIL(&faulted_env->page_WS_list, ptr);
								     		}
								     		else
								     		{
								     			break;
								     		}

								     	}

                                        //insert new element lel new page fe el ws (tail) w remove el vicitm
                                        struct WorkingSetElement * wse = env_page_ws_list_create_element(faulted_env, fault_va);

								     	LIST_INSERT_TAIL(&faulted_env->page_WS_list , wse);
								     	LIST_REMOVE(&faulted_env->page_WS_list, ws_victim_element);

								     	unmap_frame(faulted_env->env_page_directory, ws_victim_element->virtual_address);


								     	kfree(ws_victim_element);


								 }
							}


						}


				}

#endif
}
void __page_fault_handler_with_buffering(struct Env * curenv, uint32 fault_va)
{
	panic("this function is not required...!!");
}
