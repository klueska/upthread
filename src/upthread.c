/*
 * Copyright (c) 2011 The Regents of the University of California
 * Barret Rhoden <brho@cs.berkeley.edu>
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See LICENSE for details.
 */

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <assert.h>
#include <stdio.h>
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <parlib/mcs.h>
#include "upthread.h"

#define printd(...)

struct upthread_queue ready_queue = TAILQ_HEAD_INITIALIZER(ready_queue);
struct upthread_queue active_queue = TAILQ_HEAD_INITIALIZER(active_queue);
mcs_lock_t queue_lock = MCS_LOCK_INIT;
upthread_once_t init_once = BTHREAD_ONCE_INIT;
int threads_ready = 0;
int threads_active = 0;

/* Helper / local functions */
static int get_next_pid(void);
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun);

/* Pthread 2LS operations */
void pth_sched_entry(void);
void pth_thread_runnable(struct uthread *uthread);
void pth_thread_yield(struct uthread *uthread);
void pth_preempt_pending(void);
void pth_spawn_thread(uintptr_t pc_start, void *data);

struct schedule_ops upthread_sched_ops = {
	pth_sched_entry,
	pth_thread_runnable,
	pth_thread_yield,
	0, /* pth_preempt_pending, */
	0, /* pth_spawn_thread, */
};

/* Publish our sched_ops, overriding the weak defaults */
struct schedule_ops *sched_ops __attribute__((weak)) = &upthread_sched_ops;

/* Static helpers */
static void __upthread_free_stack(struct upthread_tcb *pt);
static int __upthread_allocate_stack(struct upthread_tcb *pt);

/* Called from vcore entry.  Options usually include restarting whoever was
 * running there before or running a new thread.  Events are handled out of
 * event.c (table of function pointers, stuff like that). */
void __attribute__((noreturn)) pth_sched_entry(void)
{
	if (current_uthread) {
		run_current_uthread();
		assert(0);
	}
	/* no one currently running, so lets get someone from the ready queue */
	struct upthread_tcb *new_thread = NULL;
	struct mcs_lock_qnode local_qn = {0};
	mcs_lock_lock(&queue_lock, &local_qn);
	new_thread = TAILQ_FIRST(&ready_queue);
	if (new_thread) {
		TAILQ_REMOVE(&ready_queue, new_thread, next);
		TAILQ_INSERT_TAIL(&active_queue, new_thread, next);
		threads_active++;
		threads_ready--;
	}
	mcs_lock_unlock(&queue_lock, &local_qn);
	/* Instead of yielding, you could spin, turn off the core, set an alarm,
	 * whatever.  You want some logic to decide this.  Uthread code will have
	 * helpers for this (like how we provide run_uthread()) */
	if (!new_thread) {
		/* TODO: consider doing something more intelligent here */
		printd("[P] No threads, vcore %d is yielding\n", vcore_id());
		vcore_yield(false);
		assert(0);
	}
	run_uthread((struct uthread*)new_thread);
	assert(0);
}

/* Could move this, along with start_routine and arg, into the 2LSs */
static void __upthread_run(void)
{
	struct upthread_tcb *me = upthread_self();
	upthread_exit(me->start_routine(me->arg));
}

void pth_thread_runnable(struct uthread *uthread)
{
	struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
	struct mcs_lock_qnode local_qn = {0};
	/* Insert the newly created thread into the ready queue of threads.
	 * It will be removed from this queue later when vcore_entry() comes up */
	mcs_lock_lock(&queue_lock, &local_qn);
	TAILQ_INSERT_TAIL(&ready_queue, upthread, next);
	threads_ready++;
	mcs_lock_unlock(&queue_lock, &local_qn);
	vcore_request(threads_ready);
}

static void __upthread_destroy(struct upthread_tcb *upthread)
{
	/* Cleanup the underlying uthread */
	uthread_cleanup(&upthread->uthread);

	/* Cleanup, mirroring upthread_create() */
	__upthread_free_stack(upthread);
	/* TODO: race on detach state */
	if (upthread->detached)
		free(upthread);
	else
		upthread->finished = 1;
}

/* The calling thread is yielding.  Do what you need to do to restart (like put
 * yourself on a runqueue), or do some accounting.  Eventually, this might be a
 * little more generic than just yield. */
void pth_thread_yield(struct uthread *uthread)
{
	struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
	struct mcs_lock_qnode local_qn = {0};
	/* Remove from the active list, whether exiting or yielding.  We're holding
	 * the lock throughout both list modifications (if applicable). */
	mcs_lock_lock(&queue_lock, &local_qn);
	threads_active--;
	TAILQ_REMOVE(&active_queue, upthread, next);
	if (upthread->flags & PTHREAD_EXITING) {
		mcs_lock_unlock(&queue_lock, &local_qn);
		__upthread_destroy(upthread);
	} else {
		/* Put it on the ready list (tail).  Don't do this until we are done
		 * completely with the thread, since it can be restarted somewhere else.
		 * */
		threads_ready++;
		TAILQ_INSERT_TAIL(&ready_queue, upthread, next);
		mcs_lock_unlock(&queue_lock, &local_qn);
	}
}
	
void pth_preempt_pending(void)
{
}

void pth_spawn_thread(uintptr_t pc_start, void *data)
{
}

/* Pthread interface stuff and helpers */

int upthread_attr_init(upthread_attr_t *a)
{
 	a->stacksize = BTHREAD_STACK_SIZE;
	a->detachstate = BTHREAD_CREATE_JOINABLE;
  	return 0;
}

int upthread_attr_destroy(upthread_attr_t *a)
{
	return 0;
}

static void __upthread_free_stack(struct upthread_tcb *pt)
{
//	assert(!munmap(pt->stack, pt->stacksize));
	free(pt->stack);
}

static int __upthread_allocate_stack(struct upthread_tcb *pt)
{
	assert(pt->stacksize);
//	void* stackbot = mmap(0, pt->stacksize,
//	                      PROT_READ|PROT_WRITE|PROT_EXEC,
//	                      MAP_SHARED|MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
//	if (stackbot == MAP_FAILED)
	void *stackbot = calloc(1, pt->stacksize);
	if (stackbot == NULL)
		return -1; // errno set by mmap
	pt->stack = stackbot;
	return 0;
}

// Warning, this will reuse numbers eventually
static int get_next_pid(void)
{
	static uint32_t next_pid = 0;
	return next_pid++;
}

int upthread_attr_setstacksize(upthread_attr_t *attr, size_t stacksize)
{
	attr->stacksize = stacksize;
	return 0;
}
int upthread_attr_getstacksize(const upthread_attr_t *attr, size_t *stacksize)
{
	*stacksize = attr->stacksize;
	return 0;
}

/* Do whatever init you want.  At some point call uthread_lib_init() and pass it
 * a uthread representing thread0 (int main()) */
static int upthread_lib_init(void)
{
    /* Make sure this only runs once */
    static bool initialized = false;
    if (initialized)
        return -1;
    initialized = true;

	struct mcs_lock_qnode local_qn = {0};
	upthread_t t = (upthread_t)calloc(1, sizeof(struct upthread_tcb));
	assert(t);
	t->id = get_next_pid();
	assert(t->id == 0);

	/* Put the new upthread on the active queue */
	mcs_lock_lock(&queue_lock, &local_qn);
	threads_active++;
	TAILQ_INSERT_TAIL(&active_queue, t, next);
	mcs_lock_unlock(&queue_lock, &local_qn);
    assert(!uthread_lib_init((struct uthread*)t));
    return 0;
}

/* Responible for creating the upthread and initializing its user trap frame */
int upthread_create(upthread_t* thread, const upthread_attr_t* attr,
                   void *(*start_routine)(void *), void* arg)
{
    static bool first = true;
    if (first) {
        assert(!upthread_lib_init());
        first = false;
    }

	/* Create a upthread struct */
	struct upthread_tcb *upthread;
	upthread = (upthread_t)calloc(1, sizeof(struct upthread_tcb));
	assert(upthread);

	/* Initialize the basics of the underlying uthread */
	uthread_init(&upthread->uthread);

	/* Initialize upthread state */
	upthread->stacksize = BTHREAD_STACK_SIZE;	/* default */
	upthread->id = get_next_pid();
	upthread->detached = FALSE;				/* default */
	upthread->flags = 0;
	upthread->finished = FALSE;				/* default */
	/* Respect the attributes */
	if (attr) {
		if (attr->stacksize)					/* don't set a 0 stacksize */
			upthread->stacksize = attr->stacksize;
		if (attr->detachstate == BTHREAD_CREATE_DETACHED)
			upthread->detached = TRUE;
	}
	/* allocate a stack */
	if (__upthread_allocate_stack(upthread))
		printf("We're fucked\n");

	/* Set the u_tf to start up in __upthread_run, which will call the real
	 * start_routine and pass it the arg.  Note those aren't set until later in
	 * upthread_create(). */
	init_uthread_tf(&upthread->uthread, __upthread_run, upthread->stack, upthread->stacksize); 

	upthread->start_routine = start_routine;
	upthread->arg = arg;
	uthread_runnable((struct uthread*)upthread);
	*thread = upthread;
	return 0;
}

int upthread_join(upthread_t thread, void** retval)
{
	/* Not sure if this is the right semantics.  There is a race if we deref
	 * thread and he is already freed (which would have happened if he was
	 * detached. */
	if (thread->detached) {
		printf("[upthread] trying to join on a detached upthread");
		return -1;
	}
	while (!thread->finished)
		upthread_yield();
	if (retval)
		*retval = thread->retval;
	free(thread);
	return 0;
}

int upthread_yield(void)
{
	uthread_yield(true);
	return 0;
}

int upthread_mutexattr_init(upthread_mutexattr_t* attr)
{
  attr->type = BTHREAD_MUTEX_DEFAULT;
  return 0;
}

int upthread_mutexattr_destroy(upthread_mutexattr_t* attr)
{
  return 0;
}

int upthread_attr_setdetachstate(upthread_attr_t *__attr, int __detachstate)
{
	__attr->detachstate = __detachstate;
	return 0;
}

int upthread_mutexattr_gettype(const upthread_mutexattr_t* attr, int* type)
{
  *type = attr ? attr->type : BTHREAD_MUTEX_DEFAULT;
  return 0;
}

int upthread_mutexattr_settype(upthread_mutexattr_t* attr, int type)
{
  if(type != BTHREAD_MUTEX_NORMAL)
    return EINVAL;
  attr->type = type;
  return 0;
}

int upthread_mutex_init(upthread_mutex_t* m, const upthread_mutexattr_t* attr)
{
  m->attr = attr;
  m->lock = 0;
  return 0;
}

/* Set *spun to 0 when calling this the first time.  It will yield after 'spins'
 * calls.  Use this for adaptive mutexes and such. */
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun)
{
	if ((*spun)++ == spins) {
		upthread_yield();
		*spun = 0;
	}
}

int upthread_mutex_lock(upthread_mutex_t* m)
{
	unsigned int spinner = 0;
	while(upthread_mutex_trylock(m))
		while(*(volatile size_t*)&m->lock) {
			cpu_relax();
			spin_to_sleep(BTHREAD_MUTEX_SPINS, &spinner);
		}
	return 0;
}

int upthread_mutex_trylock(upthread_mutex_t* m)
{
  return atomic_exchange_acq(&m->lock,1) == 0 ? 0 : EBUSY;
}

int upthread_mutex_unlock(upthread_mutex_t* m)
{
  /* Need to prevent the compiler (and some arches) from reordering older
   * stores */
  wmb();
  m->lock = 0;
  return 0;
}

int upthread_mutex_destroy(upthread_mutex_t* m)
{
  return 0;
}

int upthread_cond_init(upthread_cond_t *c, const upthread_condattr_t *a)
{
  c->attr = a;
  memset(c->waiters,0,sizeof(c->waiters));
  memset(c->in_use,0,sizeof(c->in_use));
  c->next_waiter = 0;
  return 0;
}

int upthread_cond_destroy(upthread_cond_t *c)
{
  return 0;
}

int upthread_cond_broadcast(upthread_cond_t *c)
{
  memset(c->waiters,0,sizeof(c->waiters));
  return 0;
}

int upthread_cond_signal(upthread_cond_t *c)
{
  int i;
  for(i = 0; i < MAX_BTHREADS; i++)
  {
    if(c->waiters[i])
    {
      c->waiters[i] = 0;
      break;
    }
  }
  return 0;
}

int upthread_cond_wait(upthread_cond_t *c, upthread_mutex_t *m)
{
  int old_waiter = c->next_waiter;
  int my_waiter = c->next_waiter;
  
  //allocate a slot
  while (atomic_exchange_acq(& (c->in_use[my_waiter]), SLOT_IN_USE) == SLOT_IN_USE)
  {
    my_waiter = (my_waiter + 1) % MAX_BTHREADS;
    assert (old_waiter != my_waiter);  // do not want to wrap around
  }
  c->waiters[my_waiter] = WAITER_WAITING;
  c->next_waiter = (my_waiter+1) % MAX_BTHREADS;  // race on next_waiter but ok, because it is advisary

  upthread_mutex_unlock(m);

  volatile int* poll = &c->waiters[my_waiter];
  while(*poll);
  c->in_use[my_waiter] = SLOT_FREE;
  upthread_mutex_lock(m);

  return 0;
}

int upthread_condattr_init(upthread_condattr_t *a)
{
  a = BTHREAD_PROCESS_PRIVATE;
  return 0;
}

int upthread_condattr_destroy(upthread_condattr_t *a)
{
  return 0;
}

int upthread_condattr_setpshared(upthread_condattr_t *a, int s)
{
  a->pshared = s;
  return 0;
}

int upthread_condattr_getpshared(upthread_condattr_t *a, int *s)
{
  *s = a->pshared;
  return 0;
}

upthread_t upthread_self()
{
  return (struct upthread_tcb*)current_uthread;
}

int upthread_equal(upthread_t t1, upthread_t t2)
{
  return t1 == t2;
}

/* This function cannot be migrated to a different vcore by the userspace
 * scheduler.  Will need to sort that shit out. */
void upthread_exit(void *ret)
{
	struct upthread_tcb *upthread = upthread_self();
	upthread->retval = ret;
	/* So our pth_thread_yield knows we want to exit */
	upthread->flags |= PTHREAD_EXITING;
	uthread_yield(false);
}

int upthread_once(upthread_once_t* once_control, void (*init_routine)(void))
{
  if(atomic_exchange_acq(once_control,1) == 0)
    init_routine();
  return 0;
}

int upthread_barrier_init(upthread_barrier_t* b, const upthread_barrierattr_t* a, int count)
{
  b->nprocs = b->count = count;
  b->sense = 0;
  upthread_mutex_init(&b->pmutex, 0);
  return 0;
}

int upthread_barrier_wait(upthread_barrier_t* b)
{
  unsigned int spinner = 0;
  int ls = !b->sense;

  upthread_mutex_lock(&b->pmutex);
  int count = --b->count;
  upthread_mutex_unlock(&b->pmutex);

  if(count == 0)
  {
    printd("Thread %d is last to hit the barrier, resetting...\n", upthread_self()->id);
    b->count = b->nprocs;
    wmb();
    b->sense = ls;
    return BTHREAD_BARRIER_SERIAL_THREAD;
  }
  else
  {
    while(b->sense != ls) {
      cpu_relax();
      spin_to_sleep(BTHREAD_BARRIER_SPINS, &spinner);
    }
    return 0;
  }
}

int upthread_barrier_destroy(upthread_barrier_t* b)
{
  upthread_mutex_destroy(&b->pmutex);
  return 0;
}

int upthread_detach(upthread_t thread)
{
	thread->detached = TRUE;
	return 0;
}

#define upthread_rwlock_t upthread_mutex_t
#define upthread_rwlockattr_t upthread_mutexattr_t
#define upthread_rwlock_destroy upthread_mutex_destroy
#define upthread_rwlock_init upthread_mutex_init
#define upthread_rwlock_unlock upthread_mutex_unlock
#define upthread_rwlock_rdlock upthread_mutex_lock
#define upthread_rwlock_wrlock upthread_mutex_lock
#define upthread_rwlock_tryrdlock upthread_mutex_trylock
#define upthread_rwlock_trywrlock upthread_mutex_trylock
