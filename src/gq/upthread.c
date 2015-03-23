#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <parlib/parlib.h>
#include <parlib/atomic.h>
#include <parlib/arch.h>
#include <parlib/mcs.h>
#include <parlib/vcore.h>
#include "internal/assert.h"
#include "upthread.h"

#define printd(...) 
//#define printd(...) printf(__VA_ARGS__)

struct upthread_queue ready_queue = STAILQ_HEAD_INITIALIZER(ready_queue);
struct upthread_queue active_queue = STAILQ_HEAD_INITIALIZER(active_queue);
struct mcs_pdr_lock queue_lock;
int threads_ready = 0;
int threads_active = 0;
bool can_adjust_vcores = TRUE;

/* Helper / local functions */
static int get_next_pid(void);
static inline void spin_to_sleep(unsigned int spins, unsigned int *spun);
static void __upthread_generic_yield(struct upthread_tcb *upthread);

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
	/* Try to get a thread.  If we get one, we'll break out and run it.  If not,
	 * we'll try to yield.  vcore_yield() might return, if we lost a race and
	 * had a new event come in, one that may make us able to get a new_thread */
	do {mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
		mcs_pdr_lock(&queue_lock, &qnode);
		new_thread = STAILQ_FIRST(&ready_queue);
		if (new_thread) {
			STAILQ_REMOVE(&ready_queue, new_thread, upthread_tcb, next);
			STAILQ_INSERT_TAIL(&active_queue, new_thread, next);
			threads_active++;
			threads_ready--;
			mcs_pdr_unlock(&queue_lock, &qnode);
			/* If you see what looks like the same uthread running in multiple
			 * places, your list might be jacked up.  Turn this on. */
			printd("[P] got uthread %08p on vc %d state %08p flags %08p\n",
			       new_thread, vcoreid,
			       ((struct uthread*)new_thread)->state,
			       ((struct uthread*)new_thread)->flags);
			break;
		}
		mcs_pdr_unlock(&queue_lock, &qnode);
		/* no new thread, try to yield */
		printd("[P] No threads, vcore %d is yielding\n", vcore_id());
		/* TODO: you can imagine having something smarter here, like spin for a
		 * bit before yielding (or not at all if you want to be greedy). */
		if (can_adjust_vcores)
			vcore_yield(FALSE);
		handle_events();
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
	mcs_pdr_lock(&queue_lock, &qnode);
	STAILQ_INSERT_TAIL(&ready_queue, upthread, next);
	threads_ready++;
	mcs_pdr_unlock(&queue_lock, &qnode);
	/* Smarter schedulers should look at the num_vcores() and how much work is
	 * going on to make a decision about how many vcores to request. */
	if (can_adjust_vcores)
		vcore_request(1);
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
	__upthread_generic_yield(upthread);
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
	__upthread_generic_yield(upthread);
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

/* Initialize thread attribute *ATTR with attributes corresponding to the
   already running thread TH.  It shall be called on uninitialized ATTR
   and destroyed with pthread_attr_destroy when no longer needed.  */
int upthread_getattr_np (upthread_t __th, upthread_attr_t *__attr)
{
	__attr->stackaddr = __th->stacktop - __th->stacksize;
	__attr->stacksize = __th->stacksize;
	if (__th->detached)
		__attr->detachstate = UPTHREAD_CREATE_DETACHED;
	else
		__attr->detachstate = UPTHREAD_CREATE_JOINABLE;
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

/* Do whatever init you want.  At some point call uthread_lib_init() and pass it
 * a uthread representing thread0 (int main()) */
static void __attribute__((constructor)) upthread_lib_init(void)
{
	mcs_pdr_init(&queue_lock);
	/* Create a upthread_tcb for the main thread */
	upthread_t t = parlib_aligned_alloc(ARCH_CL_SIZE,
	                      sizeof(struct upthread_tcb));
	assert(t);
	memset(t, 0, sizeof(struct upthread_tcb));
	t->id = get_next_pid();
	/* Fill in the main context stack info. */
	void *stackbottom;
	size_t stacksize;
	parlib_get_main_stack(&stackbottom, &stacksize);
	t->stacktop = stackbottom + stacksize;
	t->stacksize = stacksize;
	t->detached = TRUE;
	t->state = UPTH_RUNNING;
	t->joiner = 0;
	assert(t->id == 0);
	/* Put the new upthread (thread0) on the active queue */
	mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
	mcs_pdr_lock(&queue_lock, &qnode);	/* arguably, we don't need these (_S mode) */
	threads_active++;
	STAILQ_INSERT_TAIL(&active_queue, t, next);
	mcs_pdr_unlock(&queue_lock, &qnode);

	/* Publish our sched_ops, overriding the defaults */
	sched_ops = &upthread_sched_ops;

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
	upthread = parlib_aligned_alloc(ARCH_CL_SIZE, sizeof(struct upthread_tcb));
	assert(upthread);
	memset(upthread, 0, sizeof(struct upthread_tcb));
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
	mcs_pdr_lock(&queue_lock, &qnode);
	threads_active--;
	STAILQ_REMOVE(&active_queue, upthread, upthread_tcb, next);
	mcs_pdr_unlock(&queue_lock, &qnode);
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
	destroy_dtls();
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

int upthread_tid()
{
  return ((struct upthread_tcb*)current_uthread)->id;
}

static void pth_blockon_syscall(struct uthread* uthread, void *sysc)
{
  struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
  upthread->state = UPTH_BLK_SYSC;

  mcs_lock_qnode_t qnode = MCS_QNODE_INIT;
  mcs_pdr_lock(&queue_lock, &qnode);
  threads_active--;
  STAILQ_REMOVE(&active_queue, upthread, upthread_tcb, next);
  mcs_pdr_unlock(&queue_lock, &qnode);
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

int upthread_kill(upthread_t __threadid, int __signo)
{
	printf("Not yet supported on parlib!\n");
	return -1;
}

int upthread_sigmask(int how, const sigset_t *set, sigset_t *oset)
{
	printf("Not yet supported on parlib!\n");
	return -1;
}
