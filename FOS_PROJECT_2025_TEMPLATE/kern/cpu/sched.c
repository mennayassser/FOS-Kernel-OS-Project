#include "sched.h"
#include<kern/cpu/sched_helpers.h>
#include <inc/assert.h>
#include <inc/environment_definitions.h>
#include <kern/proc/user_environment.h>
#include <kern/trap/trap.h>
#include <kern/mem/kheap.h>
#include <kern/mem/memory_manager.h>
#include <kern/tests/utilities.h>
#include <kern/cmd/command_prompt.h>
#include <kern/cpu/cpu.h>
#include <kern/cpu/picirq.h>
#include <inc/dynamic_allocator.h>
#include<inc/queue.h>
#include <kern/cpu/sched.h>
#include <inc/environment_definitions.h>
#include <kern/cpu/sched_helpers.h>
uint32 m_prirr_starv = 0;
uint32 isSchedMethodRR(){return (scheduler_method == SCH_RR);}
uint32 isSchedMethodMLFQ(){return (scheduler_method == SCH_MLFQ); }
uint32 isSchedMethodBSD(){return(scheduler_method == SCH_BSD); }
uint32 isSchedMethodPRIRR(){return(scheduler_method == SCH_PRIRR); }
//===================================================================================//
//============================ SCHEDULER FUNCTIONS ==================================//
//===================================================================================//
static struct Env* (*sched_next[])(void) = {
[SCH_RR]    fos_scheduler_RR,
[SCH_MLFQ]  fos_scheduler_MLFQ,
[SCH_BSD]   fos_scheduler_BSD,
[SCH_PRIRR]   fos_scheduler_PRIRR,

};

//===================================
// [1] Default Scheduler Initializer:
//===================================
void sched_init()
{
	old_pf_counter = 0;

	sched_init_RR(INIT_QUANTUM_IN_MS);

	init_queue(&ProcessQueues.env_new_queue);
	init_queue(&ProcessQueues.env_exit_queue);

	mycpu()->scheduler_status = SCH_STOPPED;

	//2024: initialize lock to protect these Qs in MULTI-CORE case only/
	init_kspinlock(&ProcessQueues.qlock, "process queues lock");
}

//=========================
// [2] Main FOS Scheduler:
//=========================

void
fos_scheduler(void)
{
	//ensure that the scheduler is invoked while interrupt is disabled
	if (read_eflags() & FL_IF)
		panic("fos_scheduler: called while the interrupt is enabled!");

	//cprintf("inside scheduler - timer cnt = %d\n", kclock_read_cnt0());
	struct Env *p;
	struct cpu *c = mycpu();
	c->proc = 0;

	chk1();
	c->scheduler_status = SCH_STARTED;

	//This variable should be set to the next environment to be run (if any)
	struct Env* next_env = NULL;

	//2024: should be outer loop as long as there's any BLOCKED processes.
	//Ref: xv6-x86 OS
	int is_any_blocked = 0;
	do
	{
		// Enable interrupts on this processor for a while to allow BLOCKED process to resume
		// The most recent process to run may have had interrupts turned off; enable them
		// to avoid a deadlock if all processes are waiting.
		sti();

		// Check ready queue(s) looking for process to run.
		//cprintf("\n[FOS_SCHEDULER] acquire: lock status before acquire = %d\n", qlock.locked);
		acquire_kspinlock(&(ProcessQueues.qlock));  //lock: to protect ready & blocked Qs in multi-CPU
		//cprintf("ACQUIRED\n");
		do
		{
			//Get next env according to the current scheduler
			next_env = sched_next[scheduler_method]() ;

			if(next_env != NULL)
			{
				//cprintf("\nScheduler select program '%s' [%d]... clock counter = %d\n", next_env->prog_name, next_env->env_id, kclock_read_cnt0());
				// Switch to chosen process. It is the process's job to release qlock
				// and then reacquire it before jumping back to us.
				set_cpu_proc(next_env);
				switchuvm(next_env);

				//Change its status to RUNNING
				next_env->env_status = ENV_RUNNING;

				//Context switch to it
				context_switch(&(c->scheduler), next_env->context);

				//ensure that the qlock is still held after returning from the process
				if(!holding_kspinlock(&ProcessQueues.qlock))
				{
					printcallstack(&ProcessQueues.qlock);
					panic("fos_scheduler(): qlock is either not held or held by another CPU!");
				}

				//Stop the clock now till finding a next proc (if any).
				//This is to avoid clock interrupt inside the scheduler after sti() of the outer loop
				kclock_stop();
				//cprintf("\n[IEN = %d] clock is stopped! returned to scheduler after context_switch. curenv = %d\n", (read_eflags() & FL_IF) == 0? 0:1, c->proc == NULL? 0 : c->proc->env_id);

				// Process is done running for now. It should have changed its p->status before coming back.
				//If no process on CPU, switch to the kernel
				assert(get_cpu_proc() == c->proc);
				int status = c->proc->env_status ;
				assert(status != ENV_RUNNING);
				if (status == ENV_READY)
				{
					//OK... will be placed to the correct ready Q in the next iteration
				}
				else
				{
					//					cprintf("scheduler: process %d is BLOCKED/EXITED\n", c->proc->env_id);
					switchkvm();
					struct Env* e = c->proc;
					set_cpu_proc(NULL);
				}
			}
		} while(next_env);

		//2024 - check if there's any blocked process?
		is_any_blocked = 0;
		for (int i = 0; i < NENV; ++i)
		{
			if (envs[i].env_status == ENV_BLOCKED)
			{
				is_any_blocked = 1;
				break;
			}
		}
		release_kspinlock(&ProcessQueues.qlock);  //release lock: to protect ready & blocked Qs in multi-CPU
		//cprintf("\n[FOS_SCHEDULER] release: lock status after = %d\n", qlock.locked);
	} while (is_any_blocked > 0);

	//2015///No more envs... curenv doesn't exist any more! return back to command prompt
	{
		//cprintf("[sched] no envs - nothing more to do!\n");
		get_into_prompt();
	}

}

//=============================
// [3] Initialize RR Scheduler:
//=============================
void sched_init_RR(uint8 quantum)
{
	// Create 1 ready queue for the RR
	num_of_ready_queues = 1;
#if USE_KHEAP
	sched_delete_ready_queues();
	ProcessQueues.env_ready_queues = kmalloc(sizeof(struct Env_Queue));
	quantums = kmalloc(num_of_ready_queues * sizeof(uint8)) ;
#endif
	quantums[0] = quantum;
	kclock_set_quantum(quantums[0]);
	init_queue(&(ProcessQueues.env_ready_queues[0]));
	//=========================================
	//DON'T CHANGE THESE LINES=================
	uint16 cnt0 = kclock_read_cnt0_latch() ; //read after write to ensure it's set to the desired value
	cprintf("*	RR scheduler with initial clock = %d\n", cnt0);
	mycpu()->scheduler_status = SCH_STOPPED;
	scheduler_method = SCH_RR;
	//=========================================
	//=========================================
}

//===============================
// [4] Initialize MLFQ Scheduler:
//===============================
void sched_init_MLFQ(uint8 numOfLevels, uint8 *quantumOfEachLevel)
{
	panic("Not implemented yet");


	//=========================================
	//DON'T CHANGE THESE LINES=================
	uint16 cnt0 = kclock_read_cnt0_latch() ; //read after write to ensure it's set to the desired value
	cprintf("*	MLFQ scheduler with initial clock = %d\n", cnt0);
	mycpu()->scheduler_status = SCH_STOPPED;
	scheduler_method = SCH_MLFQ;
	//=========================================
	//=========================================

}

//===============================
// [5] Initialize BSD Scheduler:
//===============================
void sched_init_BSD(uint8 numOfLevels, uint8 quantum)
{
	panic("Not implemented yet");


	//=========================================
	//DON'T CHANGE THESE LINES=================
	uint16 cnt0 = kclock_read_cnt0_latch() ; //read after write to ensure it's set to the desired value
	cprintf("*	BSD scheduler with initial clock = %d\n", cnt0);
	mycpu()->scheduler_status = SCH_STOPPED;
	scheduler_method = SCH_BSD;
	//=========================================
	//=========================================
}

//======================================
//======================================
// ================================
// [6] Initialize PRIORITY RR Scheduler:
// ===============================

void sched_init_PRIRR(uint8 numOfPriorities, uint8 quantum, uint32 starvThresh)
{
#if USE_KHEAP

	sched_delete_ready_queues();


    acquire_kspinlock(&ProcessQueues.qlock);

    // Allocate the array of ready queues (queue lkol priority)
    ProcessQueues.env_ready_queues = kmalloc(numOfPriorities * sizeof(struct Env_Queue));
    release_kspinlock(&ProcessQueues.qlock);
    num_of_ready_queues = numOfPriorities;


  if (quantums == NULL)
    {
        // lw quantum allocation fails, h call kfree for every queue allocated
        kfree(ProcessQueues.env_ready_queues);
        release_kspinlock(&ProcessQueues.qlock);
        return;
    }


    quantums = kmalloc(num_of_ready_queues * sizeof(uint8)) ;
    if (quantums == NULL)
        {
            kfree(ProcessQueues.env_ready_queues);
            release_kspinlock(&ProcessQueues.qlock);

        }
    quantums[0]=quantum;
    // Initialize all ready queues
     for (int i = 0; i < num_of_ready_queues; i++) {
        init_queue(&(ProcessQueues.env_ready_queues[i]));
    }
    	// Set starv
     sched_set_starv_thresh(starvThresh);



    //=========================================
    //DON'T CHANGE THESE LINES=================
    uint16 cnt0 = kclock_read_cnt0_latch();
    cprintf("* PRIORITY RR scheduler with initial clock = %d\n", cnt0);
    mycpu()->scheduler_status = SCH_STOPPED;
    scheduler_method = SCH_PRIRR;
    //=========================================
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}



//=========================
// [7] RR Scheduler:
//=========================
struct Env* fos_scheduler_RR()
{
	// Implement simple round-robin scheduling.
	// Pick next environment from the ready queue,
	// and switch to such environment if found.
	// It's OK to choose the previously running env if no other env
	// is runnable.
	//To protect process Qs (or info of current process) in multi-CPU*/
	//if(!holding_kspinlock(&ProcessQueues.qlock))
	//	panic("fos_scheduler_RR: q.lock is not held by this CPU while it's expected to be.");
	//
	struct Env *next_env = NULL;
	struct Env *cur_env = get_cpu_proc();
	//If the curenv is still exist, then insert it again in the ready queue
	if (cur_env != NULL)
	{
		//cprintf("RR: [%d] with status %d will be added to ready Q", cur_env->env_id, cur_env->env_status);
		enqueue(&(ProcessQueues.env_ready_queues[0]), cur_env);
	}

	//Pick the next environment from the ready queue
	next_env = dequeue(&(ProcessQueues.env_ready_queues[0]));

	//Reset the quantum
	//2017: Reset the value of CNT0 for the next clock interval
	kclock_set_quantum(quantums[0]);
	//uint16 cnt0 = kclock_read_cnt0_latch() ;
	//cprintf("CLOCK INTERRUPT AFTER RESET: Counter0 Value = %d\n", cnt0 );

	return next_env;
}

//=========================
// [8] MLFQ Scheduler:
//=========================
struct Env* fos_scheduler_MLFQ()
{
	//Apply the MLFQ with the specified levels to pick up the next environment
	//Note: the "curenv" (if exist) should be placed in its correct queue
	//To protect process Qs (or info of current process) in multi-CPU*/
	if(!holding_kspinlock(&ProcessQueues.qlock))
		panic("fos_scheduler_MLFQ: q.lock is not held by this CPU while it's expected to be.");
	//
	panic("Not implemented yet");
}

//=========================
// [9] BSD Scheduler:
//=========================
struct Env* fos_scheduler_BSD()
{
	//To protect process Qs (or info of current process) in multi-CPU*/
	if(!holding_kspinlock(&ProcessQueues.qlock))
		panic("fos_scheduler_BSD: q.lock is not held by this CPU while it's expected to be.");
	//
	panic("Not implemented yet");
}

//=============================
// [10] PRIORITY RR Scheduler:
//=============================
// =========================
// [10] PRIORITY RR Scheduler:
// =========================
struct Env* fos_scheduler_PRIRR()
{
#if USE_KHEAP

    if (!holding_kspinlock(&ProcessQueues.qlock))
        panic("fos_scheduler_PRIRR: qlock is not held by this CPU while expected.");

    struct Env *mrunning = get_cpu_proc();
    struct Env *mchosen = NULL;

    // If there's a running env, move it back to ready w drop it fe priority queue ale sah
   if(mrunning !=NULL)
    {
    	  mrunning->env_status = ENV_READY;
    	 sched_insert_ready(mrunning);
    }

    // loop from highest priority (0) lhad mnla'y non empty queue
    for (int i = 0; i < num_of_ready_queues; i++)
    {
    	 if (mchosen != NULL)
    		 break;
        struct Env_Queue *q = &ProcessQueues.env_ready_queues[i];

        // If this priority has ready envs, dequeue
        if (queue_size(q)!=0) {
            mchosen = dequeue(q);
    		mchosen->env_status = ENV_RUNNING;
            break;
        }
    }

    // If no env is ready h set default quantum

        kclock_set_quantum(quantums[0]);

    return mchosen;

#else
	panic("not handled when KERN HEAP is disabled");
#endif
}


//========================================
// [11] Clock Interrupt Handler
//	  (Automatically Called Every Quantum)
//========================================


// ========================================
// [11] Clock Interrupt Handler (PRIRR path)
// ========================================
void clock_interrupt_handler(struct Trapframe* tf)
{
#if USE_KHEAP
    if (isSchedMethodPRIRR())
    {

        // Loop 3ala kol el priority queues ma3ada el highest priority (0)
        for (int priority = 1; priority < num_of_ready_queues; priority++)
        {
        	//acquire_kspinlock(&ProcessQueues.qlock);
            struct Env_Queue *mrunning = &ProcessQueues.env_ready_queues[priority];
            struct Env *mchosen=NULL;

            //de ashan a3mel iterate safely 3ala kol process inside el queue
            LIST_FOREACH_SAFE(mchosen, mrunning, Env){
                // Benzawed el clock beta3 el process kol interrupt
            	mchosen->clock++;

                // check ala starv
                if (mchosen->clock >= m_prirr_starv)
                {
                	// remove from current pr queue
                	remove_from_queue(mrunning,mchosen);
                	mchosen->priority--;
                	mchosen->clock = 0;//reset
                    enqueue(&(ProcessQueues.env_ready_queues[priority-1]), mchosen);
                }

            }
        }


        //release_kspinlock(&ProcessQueues.qlock);
    }



    /* Rest of clock handler (ticks, yield etc.) */
    ticks++;
    struct Env* p = get_cpu_proc();
    if (p == NULL)
    {
        // no running env
    }
    else
    {
        p->nClocks++ ;
        if(isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
        {
            update_WS_time_stamps();
        }
        yield();
    }
#else
	panic("not handled when KERN HEAP is disabled");
#endif
}




	/*DON'T CHANGE THESE LINES/
	ticks++ ;
	struct Env* p = get_cpu_proc();
	if (p == NULL)
	{
//		cprintf("\n??????????????????? p == NULL ?????????????????????\n");
//		cprintf("IRQ0 mask = %d\n", irq_get_mask(0));
//		cprintf("caller IEN = %d, EIP = %x\n", tf->tf_eflags & FL_IF, tf->tf_eip);
//		cprintf("scheduler status = %d\n", mycpu()->scheduler_status) ;
		//panic("clock_interrupt_handler: no running process at the cpu! unexpected clock interrupt in the kernel!");
	}
	else
	{
		p->nClocks++ ;
		if(isPageReplacmentAlgorithmLRU(PG_REP_LRU_TIME_APPROX))
		{
			update_WS_time_stamps();
		}
		//cprintf("\n*\nClock Handler\n*\n") ;
		//fos_scheduler();
		yield();
	}
	/*/


//===================================================================
// [9] Update LRU Timestamp of WS Elements
//	  (Automatically Called Every Quantum in case of LRU Time Approx)
//===================================================================
void update_WS_time_stamps()
{
	//TODO: [PROJECT'25.IM#6] FAULT HANDLER II - #1 update_WS_time_stamps
	//Your code is here
	//Comment the following line
	//panic("update_WS_time_stamps is not implemented yet...!!");
#if USE_KHEAP
	struct Env *current_env = get_cpu_proc();
	struct WorkingSetElement *ws_current_element = NULL;
	//loop 3ala kol el elements fe el ws
	LIST_FOREACH(ws_current_element , &(current_env->page_WS_list))
	{
		//newsal lel page table beta3 el current ws element
		uint32 *page_table = NULL;
		get_page_table(current_env->env_page_directory , ws_current_element->virtual_address , &page_table);

		if(page_table!=NULL)
		{
			//nero7 lel page table entry lel current element
			uint32 table_entry = page_table[PTX(ws_current_element->virtual_address)];
			int used_bit = 	(table_entry & PERM_USED) ? 1 : 0 ;
			//1)shift counter "time stamp" to right one bit ( counter / 2 )
			ws_current_element->time_stamp = (ws_current_element->time_stamp) >> 1;

			//2)put "used bit" fe the left msb
			if(used_bit)
			{
				ws_current_element->time_stamp = (used_bit << 31) | ws_current_element->time_stamp;
			}

			//3)clear "used bit" bta3 el current element fe el page table w n clear el address mn el tlb
			page_table[PTX(ws_current_element->virtual_address)] = table_entry & (~ PERM_USED);
			tlb_invalidate(current_env->env_page_directory ,(void *) ws_current_element->virtual_address);

		}

	}
#endif
}
