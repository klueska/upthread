#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <parlib/parlib.h>
#include <parlib/atomic.h>
#include <parlib/arch.h>
#include <parlib/vcore.h>
#include "internal/assert.h"
#include "upthread.h"

#define printd(...) 
//#define printd(...) printf(__VA_ARGS__)

struct upthread_lithe_sched {
	lithe_fork_join_sched_t sched;
};
typedef struct upthread_lithe_sched upthread_lithe_sched_t;

upthread_lithe_sched_t *upthread_lithe_sched;
struct wfl context_zombie_list = WFL_INITIALIZER(context_zombie_list);

static void context_exit(lithe_sched_t *__this, lithe_context_t *context);

static const lithe_sched_funcs_t upthread_lithe_sched_funcs = {
  .hart_request        = lithe_fork_join_sched_hart_request,
  .hart_enter          = lithe_fork_join_sched_hart_enter,
  .hart_return         = lithe_fork_join_sched_hart_return,
  .sched_enter         = lithe_fork_join_sched_sched_enter,
  .sched_exit          = lithe_fork_join_sched_sched_exit,
  .child_enter         = lithe_fork_join_sched_child_enter,
  .child_exit          = lithe_fork_join_sched_child_exit,
  .context_block       = lithe_fork_join_sched_context_block,
  .context_unblock     = lithe_fork_join_sched_context_unblock,
  .context_yield       = lithe_fork_join_sched_context_yield,
  .context_exit        = context_exit
};

static upthread_lithe_context_t *__ctx_alloc(size_t stacksize)
{
	upthread_lithe_context_t *ctx = wfl_remove(&context_zombie_list);
	if (!ctx) {
		int offset = ROUNDUP(sizeof(upthread_lithe_context_t), ARCH_CL_SIZE);
		offset += rand_r(&rseed(0)) % max_vcores() * ARCH_CL_SIZE;
		stacksize = ROUNDUP(stacksize + offset, PGSIZE);
		void *stackbot = mmap(
			0, stacksize, PROT_READ|PROT_WRITE|PROT_EXEC,
			MAP_PRIVATE|MAP_ANONYMOUS, -1, 0
		);
		if (stackbot == MAP_FAILED)
			abort();
		ctx = stackbot + stacksize - offset;
		ctx->context.stack_offset = offset;
		ctx->context.context.stack.bottom = stackbot;
		ctx->context.context.stack.size = stacksize - offset;
	}
    return ctx;
}

static void __ctx_free(upthread_lithe_context_t *ctx)
{
	if (wfl_size(&context_zombie_list) < 1000) {
		wfl_insert(&context_zombie_list, ctx);
	} else {
		int stacksize = ctx->context.context.stack.size
		              + ctx->context.stack_offset;
		int ret = munmap(ctx->context.context.stack.bottom, stacksize);
		assert(!ret);
	}
}

/* Pthread interface stuff and helpers */

/* Initialize thread attribute *ATTR with attributes corresponding to the
   already running thread TH.  It shall be called on uninitialized ATTR
   and destroyed with pthread_attr_destroy when no longer needed.  */
int upthread_getattr_np (upthread_t __th, upthread_attr_t *__attr)
{
	__attr->stackaddr = __th->context.context.stack.bottom;
	__attr->stacksize = __th->context.context.stack.size;
	if (__th->detached)
		__attr->detachstate = UPTHREAD_CREATE_DETACHED;
	else
		__attr->detachstate = UPTHREAD_CREATE_JOINABLE;
	return 0;
}

static upthread_lithe_sched_t *upthread_lithe_sched_alloc()
{
  /* Allocate all the scheduler data together. */
  struct sched_data {
    upthread_lithe_sched_t sched;
    upthread_lithe_context_t main_context;
    struct lithe_fork_join_vc_mgmt vc_mgmt[];
  };

  struct sched_data *s = parlib_aligned_alloc(PGSIZE,
          sizeof(*s) + sizeof(struct lithe_fork_join_vc_mgmt) * max_vcores());
  s->sched.sched.vc_mgmt = &s->vc_mgmt[0];
  s->sched.sched.sched.funcs = &upthread_lithe_sched_funcs;

  /* Initialize some pthread specific fields before initializing the generic
   * underlying fjs. */
  lithe_fork_join_sched_init(&s->sched.sched, &s->main_context.context);
  return &s->sched;
}

/* Enter the upthread lithe scheduler at boot, and only exit at the very end. */
static void __attribute__((constructor)) upthread_lib_init(void)
{
	init_once_racy(return);

	lithe_lib_init();
	upthread_lithe_sched_t *sched = upthread_lithe_sched_alloc();
	upthread_lithe_context_t *ctx =
        (upthread_lithe_context_t*) sched->sched.sched.main_context;
	ctx->detached = TRUE;
	ctx->joiner = 0;
	lithe_sched_enter(&sched->sched.sched);
	upthread_lithe_sched = sched;
}

static void __attribute__((destructor)) upthread_lib_destroy(void)
{
	init_once_racy(return);
	/* Don't even bother exiting the scheduler because we are done... */
	/* lithe_sched_exit(&upthread_lithe_sched->sched.sched); */
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
	upthread_lithe_context_t *upthread;
	upthread = __ctx_alloc(stacksize);
	upthread->detached = FALSE;				/* default */
	upthread->joiner = 0;
	/* Respect the attributes */
	if (attr) {
		if (attr->detachstate == UPTHREAD_CREATE_DETACHED)
			upthread->detached = TRUE;
	}
	upthread->start_routine = start_routine;
	upthread->arg = arg;

	void __uthread_run()
	{
		upthread_lithe_context_t *me = upthread_self();
		upthread_exit(me->start_routine(me->arg));
	}
	upthread_lithe_sched_t *sched = upthread_lithe_sched;
	lithe_fork_join_context_init(&sched->sched, &upthread->context,
                                 __uthread_run, NULL);
	*thread = upthread;
	return 0;
}

/* Callback/bottom half of join, called from __uthread_yield (vcore context).
 * join_target is who we are trying to join on (and who is calling exit). */
static void __pth_join_cb(lithe_context_t *ctx, void *arg)
{
	upthread_lithe_context_t *upthread = (upthread_lithe_context_t *)ctx;
	upthread_lithe_context_t *join_target = (upthread_lithe_context_t *)arg;
	upthread_lithe_context_t *temp_pth = 0;
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
		lithe_context_unblock(&upthread->context.context);
	}
}

int upthread_join(upthread_t join_target, void **retval)
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
		lithe_context_block(__pth_join_cb, &join_target->context.context);
		/* When we return/restart, the thread will be done */
	} else {
		assert(join_target->joiner == join_target);	/* sanity check */
	}
	if (retval)
		*retval = join_target->retval;
	__ctx_free(join_target);
	return 0;
}

/* Callback/bottom half of exit.  Syncs with __pth_join_cb.  Here's how it
 * works: the slot for joiner is initially 0.  Joiners try to swap themselves
 * into that spot.  Exiters try to put 'themselves' into it.  Whoever gets 0
 * back won the race.  If the exiter lost the race, it must wake up the joiner
 * (which was the value from temp_pth).  If the joiner lost the race, it must
 * wake itself up, and for sanity reasons can ensure the value from temp_pth is
 * the join target). */
static void context_exit(lithe_sched_t *__this, lithe_context_t *context)
{
	upthread_lithe_context_t *upthread = (upthread_lithe_context_t *)context;
	upthread_lithe_context_t *temp_pth = 0;
	/* Down the absolute hart count. */
	lithe_hart_request(-1);
	/* Destroy the upthread */
	lithe_fork_join_context_cleanup(&upthread->context);
	/* TODO: race on detach state (see join) */
	if (upthread->detached) {
		__ctx_free(upthread);
	} else {
		/* See if someone is joining on us.  If not, we're done (and the
		 * joiner will wake itself when it saw us there instead of 0). */
		temp_pth = atomic_swap_ptr((void**)&upthread->joiner, upthread);
		if (temp_pth) {
			/* they joined before we exited, we need to wake them */
			printd("[pth] %08p exiting, waking joiner %08p\n",
			       upthread, temp_pth);
			lithe_context_unblock(&temp_pth->context.context);
		}
	}
}

void upthread_exit(void *ret)
{
	upthread_lithe_context_t *upthread = upthread_self();
	upthread->retval = ret;
	destroy_dtls();
	lithe_context_exit();
}

/* Cooperative yielding of the processor, to allow other threads to run */
int upthread_yield(void)
{
	lithe_context_yield();
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
  return ((lithe_context_t*)current_uthread)->id;
}

