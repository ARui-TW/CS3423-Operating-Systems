// scheduler.cc 
//	Routines to choose the next thread to run, and to dispatch to
//	that thread.
//
// 	These routines assume that interrupts are already disabled.
//	If interrupts are disabled, we can assume mutual exclusion
//	(since we are on a uniprocessor).
//
// 	NOTE: We can't use Locks to provide mutual exclusion here, since
// 	if we needed to wait for a lock, and the lock was busy, we would 
//	end up calling FindNextToRun(), and that would put us in an 
//	infinite loop.
//
// 	Very simple implementation -- no priorities, straight FIFO.
//	Might need to be improved in later assignments.
//
// Copyright (c) 1992-1996 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "debug.h"
#include "scheduler.h"
#include "main.h"

//----------------------------------------------------------------------
// Define compare methods
//----------------------------------------------------------------------
int compare_appr_burst_time(Thread *x, Thread *y){
  return (int)((x->get_appr_burst_time()) - (y->get_appr_burst_time()));
}

int compare_priority(Thread *x, Thread *y){
  return (y->get_priority()) - (x->get_priority());
}

//----------------------------------------------------------------------
// Scheduler::Scheduler
// 	Initialize the list of ready but not running threads.
//	Initially, no ready threads.
//----------------------------------------------------------------------

Scheduler::Scheduler()
{ 
    // readyList = new List<Thread *>; 
    L1_ready_list = new SortedList<Thread *>(compare_appr_burst_time);
    L2_ready_list = new SortedList<Thread *>(compare_priority);
    L3_ready_list = new List<Thread *>;
    toBeDestroyed = NULL;
} 

//----------------------------------------------------------------------
// Scheduler::~Scheduler
// 	De-allocate the list of ready threads.
//----------------------------------------------------------------------

Scheduler::~Scheduler()
{ 
    delete readyList; 
} 

//----------------------------------------------------------------------
// Scheduler::ReadyToRun
// 	Mark a thread as ready, but not running.
//	Put it on the ready list, for later scheduling onto the CPU.
//
//	"thread" is the thread to be put on the ready list.
//----------------------------------------------------------------------

void
Scheduler::ReadyToRun (Thread *thread)
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);
    DEBUG(dbgThread, "Putting thread on ready list: " << thread->getName());
	//cout << "Putting thread on ready list: " << thread->getName() << endl ;
    thread->setStatus(READY);

    // readyList->Append(thread);

    int priority = thread->get_priority();
    // cout << thread->getID() << ": " << thread->get_priority() << endl;

    if(priority >= 100){
        L1_ready_list->Insert(thread);
        DEBUG(dbgScheduling, "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is inserted into queue " << "L[1]");
    }else if(priority >= 50){
        L2_ready_list->Insert(thread);
        DEBUG(dbgScheduling, "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is inserted into queue " << "L[2]");
    }else{
        L3_ready_list->Append(thread);
        DEBUG(dbgScheduling, "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is inserted into queue " << "L[3]");
    }

    thread -> set_start_waiting_time(kernel->stats->totalTicks);
}

//----------------------------------------------------------------------
// Scheduler::FindNextToRun
// 	Return the next thread to be scheduled onto the CPU.
//	If there are no ready threads, return NULL.
// Side effect:
//	Thread is removed from the ready list.
//----------------------------------------------------------------------

Thread *
Scheduler::FindNextToRun ()
{
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (!L1_ready_list->IsEmpty())
    {
        Thread *thread = L1_ready_list->Front();
        int new_total_waiting_time = kernel->stats->totalTicks - thread->get_start_waiting_time();
        new_total_waiting_time += thread->get_total_waiting_time();
        thread->set_total_waiting_time(new_total_waiting_time);
        DEBUG(dbgScheduling, "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is removed from queue " << "L[1]");
        return L1_ready_list->RemoveFront();
    }
    else if (!L2_ready_list->IsEmpty())
    {
        Thread *thread = L2_ready_list->Front();
        int new_total_waiting_time = kernel->stats->totalTicks - thread->get_start_waiting_time();
        new_total_waiting_time += thread->get_total_waiting_time();
        thread->set_total_waiting_time(new_total_waiting_time);
        DEBUG(dbgScheduling, "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is removed from queue " << "L[2]");
        return L2_ready_list->RemoveFront();
    }
    else if (!L3_ready_list->IsEmpty())
    {
        Thread *thread = L3_ready_list->Front();
        int new_total_waiting_time = kernel->stats->totalTicks - thread->get_start_waiting_time();
        new_total_waiting_time += thread->get_total_waiting_time();
        thread->set_total_waiting_time(new_total_waiting_time);
        DEBUG(dbgScheduling, "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is removed from queue " << "L[3]");
        return L3_ready_list->RemoveFront();
    }
    else
    {
        return NULL;
    }
}

//----------------------------------------------------------------------
// Scheduler::Run
// 	Dispatch the CPU to nextThread.  Save the state of the old thread,
//	and load the state of the new thread, by calling the machine
//	dependent context switch routine, SWITCH.
//
//      Note: we assume the state of the previously running thread has
//	already been changed from running to blocked or ready (depending).
// Side effect:
//	The global variable kernel->currentThread becomes nextThread.
//
//	"nextThread" is the thread to be put into the CPU.
//	"finishing" is set if the current thread is to be deleted
//		once we're no longer running on its stack
//		(when the next thread starts running)
//----------------------------------------------------------------------

void
Scheduler::Run (Thread *nextThread, bool finishing)
{
    Thread *oldThread = kernel->currentThread;
    
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    if (finishing) {	// mark that we need to delete current thread
         ASSERT(toBeDestroyed == NULL);
	 toBeDestroyed = oldThread;
    }
    
    if (oldThread->space != NULL) {	// if this thread is a user program,
        oldThread->SaveUserState(); 	// save the user's CPU registers
	oldThread->space->SaveState();
    }
    
    oldThread->CheckOverflow();		    // check if the old thread
					    // had an undetected stack overflow

    kernel->currentThread = nextThread;  // switch to the next thread
    nextThread->setStatus(RUNNING);      // nextThread is now running
    
    DEBUG(dbgThread, "Switching from: " << oldThread->getName() << " to: " << nextThread->getName());
    
    // This is a machine-dependent assembly language routine defined 
    // in switch.s.  You may have to think
    // a bit to figure out what happens after this, both from the point
    // of view of the thread and from the perspective of the "outside world".

    oldThread->set_accumulated_ticks(oldThread->get_accumulated_ticks() + kernel->stats->totalTicks - oldThread->get_start_running_time());
    DEBUG(dbgScheduling, "[E] Tick [" << kernel->stats->totalTicks << "]: Thread [" << nextThread->getID() <<"] is now selected for execution, thread [" << oldThread->getID() << "] is replaced, and it has executed [" << oldThread->get_accumulated_ticks() << "]  ticks");
    nextThread->set_total_waiting_time(0);
    nextThread->set_start_running_time(kernel->stats->totalTicks);

    SWITCH(oldThread, nextThread);

    // we're back, running oldThread
    oldThread->set_start_running_time(kernel->stats->totalTicks);
      
    // interrupts are off when we return from switch!
    ASSERT(kernel->interrupt->getLevel() == IntOff);

    DEBUG(dbgThread, "Now in thread: " << oldThread->getName());

    CheckToBeDestroyed();		// check if thread we were running
					// before this one has finished
					// and needs to be cleaned up
    
    if (oldThread->space != NULL) {	    // if there is an address space
        oldThread->RestoreUserState();     // to restore, do it.
	oldThread->space->RestoreState();
    }
}

//----------------------------------------------------------------------
// Scheduler::CheckToBeDestroyed
// 	If the old thread gave up the processor because it was finishing,
// 	we need to delete its carcass.  Note we cannot delete the thread
// 	before now (for example, in Thread::Finish()), because up to this
// 	point, we were still running on the old thread's stack!
//----------------------------------------------------------------------

void
Scheduler::CheckToBeDestroyed()
{
    if (toBeDestroyed != NULL) {
        delete toBeDestroyed;
	toBeDestroyed = NULL;
    }
}


//----------------------------------------------------------------------
// Scheduler::CheckPreemption
// Check if able to preempt.
//----------------------------------------------------------------------

bool Scheduler::CheckPreemption()
{
    Thread *currentThread = kernel->currentThread;
    bool preemption = false;
    if (!L1_ready_list->IsEmpty())
    {
        if (currentThread->get_priority() < 100) {
            // current thread's priority is less than threads in L1
            preemption = true;
        }
        else if (currentThread->get_appr_burst_time() > L1_ready_list->Front()->get_appr_burst_time()) {
            preemption = true;
        }
    }
    else if (!L2_ready_list->IsEmpty() && currentThread->get_priority() < 50)
    {
        preemption = true;
    }
    else if (!L3_ready_list->IsEmpty() && currentThread->get_priority() < 50)
    {
        preemption = true;
    }
    return preemption;
}

//----------------------------------------------------------------------
// Scheduler::AgingMechanism
// Check if able to preempt.
//----------------------------------------------------------------------

void
Scheduler::AgingMechanism(){
    ListIterator<Thread *> *iterator;

    iterator = new ListIterator<Thread *>(L1_ready_list);
    SortedList<Thread* > *L1_queue_tmp = new SortedList<Thread *>(compare_appr_burst_time) ;

    for (; !iterator->IsDone(); iterator->Next()) {
        Thread* thread = iterator->Item();
        int new_total_waiting_time = kernel->stats->totalTicks - thread->get_start_waiting_time();
        new_total_waiting_time += thread->get_total_waiting_time();
        thread->set_total_waiting_time(new_total_waiting_time);
        thread->set_start_waiting_time(kernel->stats->totalTicks);

        if(thread->get_total_waiting_time() >= 1500){
            int new_priority = thread->get_priority() + 10;
            if(new_priority <= 149) {
                DEBUG(dbgScheduling, "[C] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] changes its priority from [" << thread->get_priority() << "] to [" << new_priority << "]");
                thread->set_priority(new_priority);
            }
            thread->set_total_waiting_time(thread->get_total_waiting_time() - 1500);
            L1_queue_tmp->Insert(thread);
        }else{
            L1_queue_tmp->Insert(thread);
        }
    }

    delete iterator;
    delete L1_ready_list;
    L1_ready_list = L1_queue_tmp;

    iterator = new ListIterator<Thread *>(L2_ready_list);
    SortedList<Thread* > * L2_queue_tmp = new SortedList<Thread *>(compare_priority);

    for (; !iterator->IsDone(); iterator->Next()) {
        Thread* thread = iterator->Item();
        int new_total_waiting_time = kernel->stats->totalTicks - thread->get_start_waiting_time();
        new_total_waiting_time += thread->get_total_waiting_time();
        thread->set_total_waiting_time(new_total_waiting_time);
        thread->set_start_waiting_time(kernel->stats->totalTicks);

        if(thread->get_total_waiting_time() >= 1500){
            int new_priority = thread->get_priority() + 10;
            DEBUG(dbgScheduling, "[C] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] changes its priority from [" << thread->get_priority() << "] to [" << new_priority << "]");
            thread->set_priority(new_priority);
            thread->set_total_waiting_time(thread->get_total_waiting_time() - 1500);
            if (new_priority >= 100) {
                L1_ready_list->Insert(thread);
                DEBUG(dbgScheduling, "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is removed from queue " << "L[2]");
                DEBUG(dbgScheduling, "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is inserted into queue " << "L[1]");
            }
            else L2_queue_tmp->Insert(thread);
        }else{
            L2_queue_tmp->Insert(thread);
        }
    }

    delete iterator;
    delete L2_ready_list;
    L2_ready_list = L2_queue_tmp;

    iterator = new ListIterator<Thread *>(L3_ready_list);
    List<Thread* > * L3_queue_tmp = new List<Thread *>;

    for (;iterator->IsDone() == 0; iterator->Next()) {
        Thread* thread = iterator->Item();
        int new_total_waiting_time = kernel->stats->totalTicks - thread->get_start_waiting_time();
        new_total_waiting_time += thread->get_total_waiting_time();
        thread->set_total_waiting_time(new_total_waiting_time);
        thread->set_start_waiting_time(kernel->stats->totalTicks);

        if(thread->get_total_waiting_time() >= 1500){
            int new_priority = thread->get_priority() + 10;
            DEBUG(dbgScheduling, "[C] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() << "] changes its priority from [" << thread->get_priority() << "] to [" << new_priority << "]");
            thread->set_priority(new_priority);
            thread->set_total_waiting_time(thread->get_total_waiting_time() - 1500);
            if (new_priority >= 50) {
                DEBUG(dbgScheduling, "[B] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is removed from queue " << "L[3]");
                DEBUG(dbgScheduling, "[A] Tick [" << kernel->stats->totalTicks << "]: Thread [" << thread->getID() <<"] is inserted into queue " << "L[2]");
                L2_ready_list->Insert(thread);
            } 
            else L3_queue_tmp->Append(thread);
        }else{
            L3_queue_tmp->Append(thread);
        }
    }

    delete iterator;
    delete L3_ready_list;
    L3_ready_list = L3_queue_tmp;
}

//----------------------------------------------------------------------
// Scheduler::Print
// 	Print the scheduler state -- in other words, the contents of
//	the ready list.  For debugging.
//----------------------------------------------------------------------
void
Scheduler::Print()
{
    cout << "Ready list contents:\n";
    readyList->Apply(ThreadPrint);
}
