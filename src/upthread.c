#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <parlib/parlib.h>
#include <parlib/atomic.h>
#include <parlib/arch.h>
#include <parlib/mcs.h>
#include <parlib/dtls.h>
#include <parlib/vcore.h>
#include <parlib/syscall.h>
#include <parlib/alarm.h>
#include "upthread.h"

#define PREEMPT_PERIOD 1000000 // in microseconds

#define printd(...) 
//#define printd(...) printf(__VA_ARGS__)

struct upthread_queue ready_queue = TAILQ_HEAD_INITIALIZER(ready_queue);
struct upthread_queue active_queue = TAILQ_HEAD_INITIALIZER(active_queue);
struct mcs_lock queue_lock;
int threads_ready = 0;
int threads_active = 0;
bool can_adjust_vcores = TRUE;

/* Helper / local functions */
static int get_next_pid(void);
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun);

/* Linux Specific! (handle async syscall events) */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type);

/* Pthread 2LS operations */
static void pth_sched_entry(void);
static void pth_thread_runnable(struct uthread *uthread);
static void pth_thread_paused(struct uthread *uthread);
static void pth_blockon_syscall(struct uthread *uthread, void *sysc);
static void pth_thread_has_blocked(struct uthread *uthread, int flags);

struct schedule_ops upthread_sched_ops = {
	pth_sched_entry,
	pth_thread_runnable,
	pth_thread_paused,
	pth_blockon_syscall,
	pth_thread_has_blocked,
	0, /* pth_preempt_pending, */
	0, /* pth_spawn_thread, */
};

/* Publish our sched_ops, overriding the weak defaults */
struct schedule_ops *sched_ops = &upthread_sched_ops;

/* Static helpers */
static void __upthread_free_stack(struct upthread_tcb *pt);
static int __upthread_allocate_stack(struct upthread_tcb *pt);

/* Variables, types, and callbacks for the preemptive scheduler alarm */
void alarm_callback(struct alarm_waiter* awaiter);
static dtls_key_t alarm_dtls_key;
struct alarm_data {
	struct alarm_waiter awaiter;
	int vcoreid;
	bool armed;
};

void init_adata(struct alarm_data *adata) {
	init_awaiter(&adata->awaiter, alarm_callback);
	adata->awaiter.data = adata;
	adata->armed = false;
	adata->vcoreid = vcore_id();
}
void alarm_callback(struct alarm_waiter* awaiter) {
	struct alarm_data *adata = (struct alarm_data*)awaiter->data;
	long pcore = __vcore_map[adata->vcoreid];
	adata->armed = false;
	if (pcore != VCORE_UNMAPPED)
		vcore_signal(adata->vcoreid);

}

/* Called from vcore entry.  Options usually include restarting whoever was
 * running there before or running a new thread.  Events are handled out of
 * event.c (table of function pointers, stuff like that). */
void __attribute__((noreturn)) pth_sched_entry(void)
{
	/* If we don't yet have the alarm dtls set up for this vcore, go ahead and
	 * set it up */
	struct alarm_data *adata = (struct alarm_data*)get_dtls(alarm_dtls_key);
	if (adata == NULL) {
		adata = malloc(sizeof(struct alarm_data));
		assert(adata);
		init_adata(adata);
		set_dtls(alarm_dtls_key, adata);
	}

	/* If we don't have an alarm started for this vcore, go ahead and start one */
	if (!adata->armed) {
		adata->armed = true;
		set_awaiter_rel(&adata->awaiter, PREEMPT_PERIOD);
		set_alarm(&adata->awaiter);
	}
	
	/* If there is a currently running thread (i.e. restarted without explicit
     * yield) just restart the thread */
	if (current_uthread) {
		run_current_uthread();
		assert(0);
	}

	/* no one currently running, so lets get someone from the ready queue */
	struct upthread_tcb *new_thread = NULL;
	/* Try to get a thread.  If we get one, we'll break out and run it.  If not,
	 * we'll try to yield.  vcore_yield() might return, if we lost a race and
	 * had a new event come in, one that may make us able to get a new_thread */
	do {
        mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
		mcs_lock_lock(&queue_lock, &qnode);
		new_thread = TAILQ_FIRST(&ready_queue);
		if (new_thread) {
			TAILQ_REMOVE(&ready_queue, new_thread, next);
			TAILQ_INSERT_TAIL(&active_queue, new_thread, next);
			threads_active++;
			threads_ready--;
			mcs_lock_unlock(&queue_lock, &qnode);
			break;
		}
		mcs_lock_unlock(&queue_lock, &qnode);
		/* no new thread, try to yield */
		printd("[P] No threads, vcore %d is yielding\n", vcore_id());
		/* TODO: you can imagine having something smarter here, like spin for a
		 * bit before yielding (or not at all if you want to be greedy). */
		if (can_adjust_vcores)
			vcore_yield(FALSE);
	} while (1);
	assert(new_thread->state == UPTH_RUNNABLE);
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
	/* At this point, the 2LS can see why the thread blocked and was woken up in
	 * the first place (coupling these things together).  On the yield path, the
	 * 2LS was involved and was able to set the state.  Now when we get the
	 * thread back, we can take a look. */
	printd("upthread %08p runnable, state was %d\n", upthread, upthread->state);
	switch (upthread->state) {
		case (UPTH_CREATED):
		case (UPTH_BLK_YIELDING):
		case (UPTH_BLK_JOINING):
		case (UPTH_BLK_SYSC):
		case (UPTH_BLK_PAUSED):
		case (UPTH_BLK_MUTEX):
			/* can do whatever for each of these cases */
			break;
		default:
			printf("Odd state %d for upthread %p\n", upthread->state, upthread);
	}
	upthread->state = UPTH_RUNNABLE;
	/* Insert the newly created thread into the ready queue of threads.
	 * It will be removed from this queue later when vcore_entry() comes up */
    mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
	mcs_lock_lock(&queue_lock, &qnode);
	TAILQ_INSERT_TAIL(&ready_queue, upthread, next);
	threads_ready++;
	mcs_lock_unlock(&queue_lock, &qnode);
	/* Smarter schedulers should look at the num_vcores() and how much work is
	 * going on to make a decision about how many vcores to request. */
	if (can_adjust_vcores)
		vcore_request(threads_ready);
}

/* For some reason not under its control, the uthread stopped running (compared
 * to yield, which was caused by uthread/2LS code).
 *
 * The main case for this is if the vcore was preempted or if the vcore it was
 * running on needed to stop.  You are given a uthread that looks like it took a
 * notif, and had its context/silly state copied out to the uthread struct.
 * (copyout_uthread).  Note that this will be called in the context (TLS) of the
 * vcore that is losing the uthread.  If that vcore is running, it'll be in a
 * preempt-event handling loop (not in your 2LS code).  If this is a big
 * problem, I'll change it. */
void pth_thread_paused(struct uthread *uthread)
{
	struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
	/* Remove from the active list.  Note that I don't particularly care about
	 * the active list.  We keep it around because it causes bugs and keeps us
	 * honest.  After all, some 2LS may want an active list */
    mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
	mcs_lock_lock(&queue_lock, &qnode);
	threads_active--;
	TAILQ_REMOVE(&active_queue, upthread, next);
	mcs_lock_unlock(&queue_lock, &qnode);
	/* communicate to pth_thread_runnable */
	upthread->state = UPTH_BLK_PAUSED;
	/* At this point, you could do something clever, like put it at the front of
	 * the runqueue, see if it was holding a lock, do some accounting, or
	 * whatever. */
	uthread_runnable(uthread);
}

void pth_thread_has_blocked(struct uthread *uthread, int flags)
{
	struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
	/* could imagine doing something with the flags.  For now, we just treat all
	 * externally blocked reasons as 'MUTEX'.  Whatever we do here, we are
	 * mostly communicating to our future selves in pth_thread_runnable(), which
	 * gets called by whoever triggered this callback */
	upthread->state = UPTH_BLK_MUTEX;
	/* Just for yucks: */
	if (flags == UTH_EXT_BLK_JUSTICE)
		printf("For great justice!\n");
}

void pth_preempt_pending(void)
{
}

void pth_spawn_thread(uintptr_t pc_start, void *data)
{
}

/* Akaros upthread extensions / hacks */

/* Tells the upthread 2LS to not change the number of vcores.  This means it will
 * neither request vcores nor yield vcores.  Only used for testing. */
void upthread_can_vcore_request(bool can)
{
	/* checked when we would request or yield */
	can_adjust_vcores = can;
}

/* Pthread interface stuff and helpers */

int upthread_attr_init(upthread_attr_t *a)
{
 	a->stacksize = UPTHREAD_STACK_SIZE;
	a->detachstate = UPTHREAD_CREATE_JOINABLE;
  	return 0;
}

int upthread_attr_destroy(upthread_attr_t *a)
{
	return 0;
}

static void __upthread_free_stack(struct upthread_tcb *pt)
{
	assert(!munmap(pt->stacktop - pt->stacksize, pt->stacksize));
}

static int __upthread_allocate_stack(struct upthread_tcb *pt)
{
	assert(pt->stacksize);
	void* stackbot = mmap(0, pt->stacksize,
	                      PROT_READ|PROT_WRITE|PROT_EXEC,
	                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (stackbot == MAP_FAILED)
		return -1; // errno set by mmap
	pt->stacktop = stackbot + pt->stacksize;
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
static void __attribute__((constructor)) upthread_lib_init(void)
{
	/* Set up the dtls key for use by each vcore for the alarm data used by
 	 * this scheduler */
	alarm_dtls_key = dtls_key_create(free);

	mcs_lock_init(&queue_lock);
	/* Create a upthread_tcb for the main thread */
	upthread_t t = (upthread_t)calloc(1, sizeof(struct upthread_tcb));
	assert(t);
	t->id = get_next_pid();
	t->stacksize = -1;
	t->stacktop = (void*)0xdeadbeef;
	t->detached = TRUE;
	t->state = UPTH_RUNNING;
	t->joiner = 0;
	assert(t->id == 0);
	/* Put the new upthread (thread0) on the active queue */
	mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
	mcs_lock_lock(&queue_lock, &qnode);	/* arguably, we don't need these (_S mode) */
	threads_active++;
	TAILQ_INSERT_TAIL(&active_queue, t, next);
	mcs_lock_unlock(&queue_lock, &qnode);

  /* Handle syscall events. */
  /* These functions are declared in parlib for simulating async syscalls on linux */
  ev_handlers[EV_SYSCALL] = pth_handle_syscall;

	/* Initialize the uthread code (we're in _M mode after this).  Doing this
	 * last so that all the event stuff is ready when we're in _M mode.  Not a
	 * big deal one way or the other.  Note that vcore_init() hasn't happened
	 * yet, so if a 2LS somehow wants to have its init stuff use things like
	 * vcore stacks or TLSs, we'll need to change this. */
	uthread_lib_init((struct uthread*)t);
}

int upthread_create(upthread_t *thread, const upthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
	/* Create the actual thread */
	struct upthread_tcb *upthread;
	upthread = (upthread_t)calloc(1, sizeof(struct upthread_tcb));
	assert(upthread);
	upthread->stacksize = UPTHREAD_STACK_SIZE;	/* default */
	upthread->state = UPTH_CREATED;
	upthread->id = get_next_pid();
	upthread->detached = FALSE;				/* default */
	upthread->joiner = 0;
	/* Respect the attributes */
	if (attr) {
		if (attr->stacksize)					/* don't set a 0 stacksize */
			upthread->stacksize = attr->stacksize;
		if (attr->detachstate == UPTHREAD_CREATE_DETACHED)
			upthread->detached = TRUE;
	}
	/* allocate a stack */
	if (__upthread_allocate_stack(upthread))
		printf("We're fucked\n");
	/* Set the u_tf to start up in __upthread_run, which will call the real
	 * start_routine and pass it the arg.  Note those aren't set until later in
	 * upthread_create(). */
	init_uthread_tf(&upthread->uthread, __upthread_run,
	                upthread->stacktop - upthread->stacksize,
                    upthread->stacksize);
	upthread->start_routine = start_routine;
	upthread->arg = arg;
	/* Initialize the uthread */
	uthread_init((struct uthread*)upthread);
	uthread_runnable((struct uthread*)upthread);
	*thread = upthread;
	return 0;
}

/* Helper that all upthread-controlled yield paths call.  Just does some
 * accounting.  This is another example of how the much-loathed (and loved)
 * active queue is keeping us honest. */
static void __upthread_generic_yield(struct upthread_tcb *upthread)
{
    mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
	mcs_lock_lock(&queue_lock, &qnode);
	threads_active--;
	TAILQ_REMOVE(&active_queue, upthread, next);
	mcs_lock_unlock(&queue_lock, &qnode);
}

/* Callback/bottom half of join, called from __uthread_yield (vcore context).
 * join_target is who we are trying to join on (and who is calling exit). */
static void __pth_join_cb(struct uthread *uthread, void *arg)
{
	struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
	struct upthread_tcb *join_target = (struct upthread_tcb*)arg;
	struct upthread_tcb *temp_pth = 0;
	__upthread_generic_yield(upthread);
	/* We're trying to join, yield til we get woken up */
	upthread->state = UPTH_BLK_JOINING;	/* could do this front-side */
	/* Put ourselves in the join target's joiner slot.  If we get anything back,
	 * we lost the race and need to wake ourselves.  Syncs with __pth_exit_cb.*/
	temp_pth = atomic_swap_ptr((void**)&join_target->joiner, upthread);
	/* After that atomic swap, the upthread might be woken up (if it succeeded),
	 * so don't touch upthread again after that (this following if () is okay).*/
	if (temp_pth) {		/* temp_pth != 0 means they exited first */
		assert(temp_pth == join_target);	/* Sanity */
		/* wake ourselves, not the exited one! */
		printd("[pth] %08p already exit, rewaking ourselves, joiner %08p\n",
		       temp_pth, upthread);
		uthread_runnable(uthread);	/* wake ourselves */
	}
}

int upthread_join(struct upthread_tcb *join_target, void **retval)
{
	/* Not sure if this is the right semantics.  There is a race if we deref
	 * join_target and he is already freed (which would have happened if he was
	 * detached. */
	if (join_target->detached) {
		printf("[upthread] trying to join on a detached upthread");
		return -1;
	}
	/* See if it is already done, to avoid the pain of a uthread_yield() (the
	 * early check is an optimization, pth_thread_yield() handles the race). */
	if (!join_target->joiner) {
		uthread_yield(TRUE, __pth_join_cb, join_target);
		/* When we return/restart, the thread will be done */
	} else {
		assert(join_target->joiner == join_target);	/* sanity check */
	}
	if (retval)
		*retval = join_target->retval;
	free(join_target);
	return 0;
}

/* Callback/bottom half of exit.  Syncs with __pth_join_cb.  Here's how it
 * works: the slot for joiner is initially 0.  Joiners try to swap themselves
 * into that spot.  Exiters try to put 'themselves' into it.  Whoever gets 0
 * back won the race.  If the exiter lost the race, it must wake up the joiner
 * (which was the value from temp_pth).  If the joiner lost the race, it must
 * wake itself up, and for sanity reasons can ensure the value from temp_pth is
 * the join target). */
static void __pth_exit_cb(struct uthread *uthread, void *junk)
{
	struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
	struct upthread_tcb *temp_pth = 0;
	__upthread_generic_yield(upthread);
	/* Catch some bugs */
	upthread->state = UPTH_EXITING;
	/* Destroy the upthread */
	uthread_cleanup(uthread);
	/* Cleanup, mirroring upthread_create() */
	__upthread_free_stack(upthread);
	/* TODO: race on detach state (see join) */
	if (upthread->detached) {
		free(upthread);
	} else {
		/* See if someone is joining on us.  If not, we're done (and the
		 * joiner will wake itself when it saw us there instead of 0). */
		temp_pth = atomic_swap_ptr((void**)&upthread->joiner, upthread);
		if (temp_pth) {
			/* they joined before we exited, we need to wake them */
			printd("[pth] %08p exiting, waking joiner %08p\n",
			       upthread, temp_pth);
			uthread_runnable((struct uthread*)temp_pth);
		}
	}
}

void upthread_exit(void *ret)
{
	struct upthread_tcb *upthread = upthread_self();
	upthread->retval = ret;
	uthread_yield(FALSE, __pth_exit_cb, 0);
}

/* Callback/bottom half of yield.  For those writing these pth callbacks, the
 * minimum is call generic, set state (communicate with runnable), then do
 * something that causes it to be runnable in the future (or right now). */
static void __pth_yield_cb(struct uthread *uthread, void *junk)
{
	struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
	__upthread_generic_yield(upthread);
	upthread->state = UPTH_BLK_YIELDING;
	/* just immediately restart it */
	uthread_runnable(uthread);
}

/* Cooperative yielding of the processor, to allow other threads to run */
int upthread_yield(void)
{
	uthread_yield(TRUE, __pth_yield_cb, 0);
	return 0;
}

int upthread_mutexattr_init(upthread_mutexattr_t* attr)
{
  attr->type = UPTHREAD_MUTEX_DEFAULT;
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
  *type = attr ? attr->type : UPTHREAD_MUTEX_DEFAULT;
  return 0;
}

int upthread_mutexattr_settype(upthread_mutexattr_t* attr, int type)
{
  if(type != UPTHREAD_MUTEX_NORMAL)
    return EINVAL;
  attr->type = type;
  return 0;
}

int upthread_mutex_init(upthread_mutex_t* m, const upthread_mutexattr_t* attr)
{
  m->attr = attr;
  atomic_init(&m->lock, 0);
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
			spin_to_sleep(UPTHREAD_MUTEX_SPINS, &spinner);
		}
	/* normally we'd need a wmb() and a wrmb() after locking, but the
	 * atomic_swap handles the CPU mb(), so just a cmb() is necessary. */
	cmb();
	return 0;
}

int upthread_mutex_trylock(upthread_mutex_t* m)
{
  return atomic_swap(&m->lock, 1) == 0 ? 0 : EBUSY;
}

int upthread_mutex_unlock(upthread_mutex_t* m)
{
  /* keep reads and writes inside the protected region */
  rwmb();
  wmb();
  atomic_set(&m->lock, 0);
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
  for(i = 0; i < MAX_UPTHREADS; i++)
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
  uint32_t old_waiter = c->next_waiter;
  uint32_t my_waiter = c->next_waiter;
  
  //allocate a slot
  while (atomic_swap_u32(& (c->in_use[my_waiter]), SLOT_IN_USE) == SLOT_IN_USE)
  {
    my_waiter = (my_waiter + 1) % MAX_UPTHREADS;
    assert (old_waiter != my_waiter);  // do not want to wrap around
  }
  c->waiters[my_waiter] = WAITER_WAITING;
  c->next_waiter = (my_waiter+1) % MAX_UPTHREADS;  // race on next_waiter but ok, because it is advisary

  upthread_mutex_unlock(m);

  volatile uint32_t* poll = &c->waiters[my_waiter];
  while(*poll);
  c->in_use[my_waiter] = SLOT_FREE;
  upthread_mutex_lock(m);

  return 0;
}

int upthread_condattr_init(upthread_condattr_t *a)
{
  a = UPTHREAD_PROCESS_PRIVATE;
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

int upthread_once(upthread_once_t* once_control, void (*init_routine)(void))
{
  if (atomic_swap_u32(once_control, 1) == 0)
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
    return UPTHREAD_BARRIER_SERIAL_THREAD;
  }
  else
  {
    while(b->sense != ls) {
      cpu_relax();
      spin_to_sleep(UPTHREAD_BARRIER_SPINS, &spinner);
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
	/* TODO: race on this state.  Someone could be trying to join now */
	thread->detached = TRUE;
	return 0;
}

static void pth_blockon_syscall(struct uthread* uthread, void *sysc)
{
  struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
  upthread->state = UPTH_BLK_SYSC;

  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_lock_lock(&queue_lock, &qnode);
  threads_active--;
  TAILQ_REMOVE(&active_queue, upthread, next);
  mcs_lock_unlock(&queue_lock, &qnode);
  /* Set things up so we can wake this thread up later */
  ((struct syscall*)sysc)->u_data = uthread;
}

/* Restarts a uthread hanging off a syscall.  For the simple pthread case, we
 * just make it runnable and let the main scheduler code handle it. */
static void restart_thread(struct syscall *sysc)
{
  struct uthread *ut_restartee = (struct uthread*)sysc->u_data;
  /* uthread stuff here: */
  assert(ut_restartee);
  assert(((struct upthread_tcb*)ut_restartee)->state == UPTH_BLK_SYSC);
  assert(ut_restartee->sysc == sysc); /* set in uthread.c */
  ut_restartee->sysc = 0; /* so we don't 'reblock' on this later */
  pth_thread_runnable(ut_restartee);
}

/* This handler is usually run in vcore context, though I can imagine it being
 * called by a uthread in some other threading library. */
static void pth_handle_syscall(struct event_msg *ev_msg, unsigned int ev_type)
{
  struct syscall *sysc;
  assert(in_vcore_context());
  /* if we just got a bit (not a msg), it should be because the process is
   * still an SCP and hasn't started using the MCP ev_q yet (using the simple
   * ev_q and glibc's blockon) or because the bit is still set from an old
   * ev_q (blocking syscalls from before we could enter vcore ctx).  Either
   * way, just return.  Note that if you screwed up the pth ev_q and made it
   * NO_MSG, you'll never notice (we used to assert(ev_msg)). */
  if (!ev_msg)
    return;
  /* It's a bug if we don't have a msg (we're handling a syscall bit-event) */
  assert(ev_msg);
  /* Get the sysc from the message and just restart it */
  sysc = ev_msg->ev_arg3;
  assert(sysc);
  restart_thread(sysc);
}

