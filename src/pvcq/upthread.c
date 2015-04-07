#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/queue.h>
#include <sys/mman.h>
#include <parlib/parlib.h>
#include <parlib/atomic.h>
#include <parlib/arch.h>
#include <parlib/vcore.h>
#include <parlib/waitfreelist.h>
#include "internal/assert.h"
#include "upthread.h"

#define printd(...) 
//#define printd(...) printf(__VA_ARGS__)

static struct wfl zombie_list = WFL_INITIALIZER(zombie_list);

struct vc_mgmt {
	struct upthread_queue tqueue;
	spin_pdr_lock_t tqlock;
	int tqsize;
	unsigned int rseed;
} __attribute__((aligned(ARCH_CL_SIZE)));
struct vc_mgmt *vc_mgmt;
#define tqueue(i) (vc_mgmt[i].tqueue)
#define tqlock(i) (vc_mgmt[i].tqlock)
#define tqsize(i) (vc_mgmt[i].tqsize)
#define rseed(i)  (vc_mgmt[i].rseed)

//static bool can_adjust_vcores = FALSE;//TRUE;
//static bool can_steal = TRUE;
static bool can_steal = TRUE;
static bool can_adjust_vcores = TRUE;
static bool ss_yield = TRUE;
static int nr_vcores = 0;
static volatile int next_queue_id = 0;
static int max_spin_count = 100;

/* Helper / local functions */
static int get_next_pid(void);
static void __upthread_generic_yield(upthread_t upthread);

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

// Warning, this will reuse numbers eventually
static int get_next_pid(void)
{
	static uint32_t next_pid = 0;
	return next_pid++;
}

static struct upthread_tcb *__upthread_alloc(size_t stacksize)
{
	// TODO wfl currently assumes stacksize the same for all contexts
	struct upthread_tcb * upthread = wfl_remove(&zombie_list);
	if (!upthread) {
		int offset = ROUNDUP(sizeof(struct upthread_tcb), ARCH_CL_SIZE);
		offset += rand_r(&rseed(0)) % max_vcores() * ARCH_CL_SIZE;
		stacksize = ROUNDUP(stacksize + offset, PGSIZE);
		void *stackbot = mmap(
			0, stacksize, PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_PRIVATE|MAP_ANONYMOUS, -1, 0
		);
		if (stackbot == MAP_FAILED)
			abort();
		upthread = stackbot + stacksize - offset;
		upthread->stack_offset = offset;
		upthread->stacktop = upthread;
		upthread->stacksize = stacksize - offset;
	}
	return upthread;
}

static void __upthread_free(struct upthread_tcb *pt)
{
	if (wfl_size(&zombie_list) < 1000) {
		wfl_insert(&zombie_list, pt);
	 } else {
		int stacksize = pt->stacksize + pt->stack_offset;
		int ret = munmap(pt->stacktop - pt->stacksize, stacksize);
		assert(!ret);
	}
}

int get_next_queue_id_basic(struct upthread_tcb *upthread)
{
	while (1) {
		int id = next_queue_id;
		int next_id = id + 1 == nr_vcores ? 0 : id + 1;
		if (__sync_bool_compare_and_swap(&next_queue_id, id, next_id))
			return id;
	}
}

int get_next_queue_id_tls_aware(struct upthread_tcb *upthread)
{
  static const int l1cachesize = 0x8000;
	static int sid = 0;
	static int cid = 0;
	int vcoreid = vcore_id();
	int hvc = max_vcores()/2;
	long utls = (long)upthread->uthread.tls_desc % l1cachesize;
	for (int i=0; (i < nr_vcores) && (i < hvc); i++) {
		int j = i + hvc;
		int tqsizei = i != vcoreid ? tqsize(i) : tqsize(i) + 1;
		int tqsizecid = cid != vcoreid ? tqsize(cid) : tqsize(cid) + 1;
		int tqsizesid = sid != vcoreid ? tqsize(sid) : tqsize(sid) + 1;
		long pvtls = (long)vcore_tls_descs(i) % l1cachesize;
		if (j < nr_vcores) {
			int tqsizej = j != vcoreid ? tqsize(j) : tqsize(j) + 1;
			long svtls = (long)vcore_tls_descs(j) % l1cachesize;
			if ((utls != pvtls) && (utls != svtls)) {
				if (tqsizei < tqsizecid)
					cid = i;
				else if (tqsizej < tqsizecid)
					cid = j;
			}
			if (tqsizei < tqsizesid)
				sid = i;
			else if (tqsizej < tqsizesid)
				sid = j;
		} else {
			if (utls != pvtls) {
				if (tqsizei < tqsizecid)
					cid = i;
			}
			if (tqsizei < tqsizesid)
				sid = i;
		}
	}
	if (tqsize(cid) <= tqsize(sid))
		return cid;
	return sid;
}

static int __pth_thread_enqueue(struct upthread_tcb *upthread, bool athead)
{
	int state = upthread->state;
	upthread->state = UPTH_RUNNABLE;

	if (state == UPTH_CREATED)
		upthread->preferred_vcq = get_next_queue_id_basic(upthread);

	int vcoreid = upthread->preferred_vcq;
	spin_pdr_lock(&tqlock(vcoreid));
	if (athead)
		STAILQ_INSERT_HEAD(&tqueue(vcoreid), upthread, next);
	else
		STAILQ_INSERT_TAIL(&tqueue(vcoreid), upthread, next);
	tqsize(vcoreid)++;
	spin_pdr_unlock(&tqlock(vcoreid));

	return vcoreid;
}

static struct upthread_tcb *__pth_thread_dequeue()
{
	inline struct upthread_tcb *tdequeue(int vcoreid)
	{
		struct upthread_tcb *upthread = NULL;
		if (tqsize(vcoreid)) {
			spin_pdr_lock(&tqlock(vcoreid));
			if ((upthread = STAILQ_FIRST(&tqueue(vcoreid)))) {
				STAILQ_REMOVE_HEAD(&tqueue(vcoreid), next);
				tqsize(vcoreid)--;
			}
			spin_pdr_unlock(&tqlock(vcoreid));
		}
		if (upthread)
			upthread->preferred_vcq = vcore_id();
		return upthread;
	}

	int vcoreid = vcore_id();
	struct upthread_tcb *upthread;

	/* Try and grab a thread from our queue */
	upthread = tdequeue(vcoreid);

	/* If there isn't one, try and steal one from someone else's queue. This
	 * assumes nr_vcores is pretty stable across the whole run for high
	 * performance.  */
	if (can_steal && !upthread) {

		/* Steal up to half of the threads in the queue and return the first */
		struct upthread_tcb *steal_threads(int vcoreid)
		{
			struct upthread_tcb *upthread = NULL;
			int num_to_steal = (tqsize(vcoreid) + 1) / 2;
			if (num_to_steal) {
				upthread = tdequeue(vcoreid);
				if (upthread) {
					for (int i=1; i<num_to_steal; i++) {
						struct upthread_tcb *u = tdequeue(vcoreid);
						if (u) __pth_thread_enqueue(u, false);
						else break;
					}
				}
			}
			return upthread;
		}

		/* First try doing power of two choices. */
		int choice[2] = { rand_r(&rseed(vcoreid)) % num_vcores(),
		                  rand_r(&rseed(vcoreid)) % num_vcores()};
		int size[2] = { tqsize(choice[0]),
		                tqsize(choice[1])};
		int id = (size[0] > size[1]) ? 0 : 1;
		if (vcoreid != choice[id])
			upthread = steal_threads(choice[id]);
		else
			upthread = steal_threads(choice[!id]);

		/* Fall back to looping through all vcores. This time I go through
		 * max_vcores() just to make sure I don't miss anything. */
		if (!upthread) {
			int i = (vcoreid + 1) % max_vcores();
			while(i != vcoreid) {
				upthread = steal_threads(i);
				if (upthread) break;
				i = (i + 1) % max_vcores();
			}
		}
	}
	return upthread;
}

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
	int spin_count = max_spin_count;
	do {
		if ((new_thread = __pth_thread_dequeue()))
			break;
		/* no new thread, try to yield */
		printd("[P] No threads, vcore %d is yielding\n", vcore_id());
		/* TODO: you can imagine having something smarter here, like spin for a
		 * bit before yielding (or not at all if you want to be greedy). */
		if (can_adjust_vcores && !(spin_count--))
			vcore_yield(FALSE);
		handle_events();
		cpu_relax();
	} while (1);
	assert(new_thread->state == UPTH_RUNNABLE);
	new_thread->state = UPTH_RUNNING;
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
	int state = upthread->state;
	int qid = 0;
	/* At this point, the 2LS can see why the thread blocked and was woken up in
	 * the first place (coupling these things together).  On the yield path, the
	 * 2LS was involved and was able to set the state.  Now when we get the
	 * thread back, we can take a look. */
	printd("upthread %08p runnable, state was %d\n", upthread, upthread->state);
	switch (state) {
		case (UPTH_BLK_SYSC):
			qid = __pth_thread_enqueue(upthread, true);
			break;
		case (UPTH_CREATED):
		case (UPTH_BLK_YIELDING):
		case (UPTH_BLK_JOINING):
		case (UPTH_BLK_PAUSED):
		case (UPTH_BLK_MUTEX):
			qid = __pth_thread_enqueue(upthread, false);
			break;
		default:
			printf("Odd state %d for upthread %p\n", upthread->state, upthread);
	}

	/* Smarter schedulers should look at the num_vcores() and how much work is
	 * going on to make a decision about how many vcores to request. */
	if (can_adjust_vcores)
		vcore_request_specific(qid);
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

/* Tells the upthread 2LS not to do any work stealing. Only run threads placed
 * in your local pvc queue. */
void upthread_can_vcore_steal(bool can)
{
	/* checked when we would request or yield */
	can_steal = can;
}

/* Tells the upthread 2LS how many vcores to consider when placing newly
 * created threads in the per vcore run queues. */
void upthread_set_num_vcores(int num)
{
	nr_vcores = MIN(num, max_vcores());
	next_queue_id = nr_vcores > 1 ? 1 : 0;
}

/* Tells the upthread 2LS to optimize the yield path with a short circuit if
 * it's local pvc queue is empty.  In this case it will not actually doa
 * context switch out of itself and back in.  */
void upthread_short_circuit_yield(bool ss)
{
	ss_yield = ss;
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

/* Do whatever init you want.  At some point call uthread_lib_init() and pass it
 * a uthread representing thread0 (int main()) */
static void __attribute__((constructor)) upthread_lib_init(void)
{
	vcore_lib_init();
	vc_mgmt = parlib_aligned_alloc(PGSIZE,
	              sizeof(struct vc_mgmt) * max_vcores());
	for (int i=0; i < max_vcores(); i++) {
		STAILQ_INIT(&tqueue(i));
		spin_pdr_init(&tqlock(i));
		tqsize(i) = 0;
		rseed(i) = i;
	}
	upthread_set_num_vcores(max_vcores());

	/* Update the spin count if passed in through the environment. */
	const char *spin_count_string = getenv("UPTHREAD_SPIN_COUNT");
	if (spin_count_string != NULL)
		max_spin_count = atoi(spin_count_string);

	/* Create a upthread_tcb for the main thread */
	upthread_t t = __upthread_alloc(0);
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
	size_t stacksize = UPTHREAD_STACK_SIZE;
	/* Get the stack size if a different one passed in */
	if (attr) {
		if (attr->stacksize) /* don't set a 0 stacksize */
			stacksize = attr->stacksize;
	}

	/* Create the actual thread */
	struct upthread_tcb *upthread;
	upthread = __upthread_alloc(stacksize);
	upthread->state = UPTH_CREATED;
	upthread->id = get_next_pid();
	upthread->detached = FALSE;				/* default */
	upthread->joiner = 0;
	/* Respect the attributes */
	if (attr) {
		if (attr->detachstate == UPTHREAD_CREATE_DETACHED)
			upthread->detached = TRUE;
	}
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
 * accounting. */
static void __upthread_generic_yield(struct upthread_tcb *upthread)
{
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
	__upthread_free(join_target);
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
	/* TODO: race on detach state (see join) */
	if (upthread->detached) {
		__upthread_free(upthread);
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
	/* Quick optimization to NOT yield if there is nothing else to run... */
	if (ss_yield && !tqsize(vcore_id()))
		return 0;
	/* Do the actual yield */
	uthread_yield(TRUE, __pth_yield_cb, 0);
	return 1;
}

int upthread_detach(upthread_t thread)
{
	/* TODO: race on this state.  Someone could be trying to join now */
	thread->detached = TRUE;
	return 0;
}

int upthread_tid()
{
  return ((struct upthread_tcb*)current_uthread)->id;
}

static void pth_blockon_syscall(struct uthread* uthread, void *sysc)
{
  struct upthread_tcb *upthread = (struct upthread_tcb*)uthread;
  __upthread_generic_yield(upthread);
  upthread->state = UPTH_BLK_SYSC;

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
