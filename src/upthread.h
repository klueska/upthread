#ifndef _UUPTHREAD_H
#define _UUPTHREAD_H

#include <sys/queue.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>
#include <parlib/mcs.h>
//#include <parlib/syscall.h>

#ifdef __cplusplus
  extern "C" {
#endif

/* Pthread states.  These are mostly examples for other 2LSs */
#define UPTH_CREATED		1
#define UPTH_RUNNABLE		2
#define UPTH_RUNNING		3
#define UPTH_EXITING		4
#define UPTH_BLK_YIELDING	5	/* brief state btw pth_yield and pth_runnable */
#define UPTH_BLK_JOINING	6	/* joining on a child */
#define UPTH_BLK_SYSC		7	/* blocked on a syscall */
#define UPTH_BLK_MUTEX		8	/* blocked externally, possibly on a mutex */
#define UPTH_BLK_PAUSED		9	/* handed back to us from uthread code */

/* Pthread struct.  First has to be the uthread struct, which the vcore code
 * will access directly (as if upthread_tcb is a struct uthread). */
struct upthread_tcb;
struct upthread_tcb {
	struct uthread uthread;
	TAILQ_ENTRY(upthread_tcb) next;
	int state;
	bool detached;
	struct upthread_tcb *joiner;			/* raced on by exit and join */
	uint32_t id;
	uint32_t stacksize;
	void *stacktop;
	void *(*start_routine)(void*);
	void *arg;
	void *retval;
};
typedef struct upthread_tcb* upthread_t;
TAILQ_HEAD(upthread_queue, upthread_tcb);

/* Per-vcore data structures to manage syscalls.  The ev_q is where we tell the
 * kernel to signal us.  We don't need a lock since this is per-vcore and
 * accessed in vcore context. */
struct sysc_mgmt {
	struct event_queue 			*ev_q;
};

#define UPTHREAD_ONCE_INIT 0
#define UPTHREAD_BARRIER_SERIAL_THREAD 12345
#define UPTHREAD_MUTEX_INITIALIZER {0,0}
#define UPTHREAD_MUTEX_NORMAL 0
#define UPTHREAD_MUTEX_DEFAULT UPTHREAD_MUTEX_NORMAL
#define UPTHREAD_MUTEX_SPINS 100 // totally arbitrary
#define UPTHREAD_BARRIER_SPINS 100 // totally arbitrary
#define UPTHREAD_COND_INITIALIZER {0,{0},{0},0}
#define UPTHREAD_PROCESS_PRIVATE 0

typedef struct
{
  int type;
} upthread_mutexattr_t;

typedef struct
{
  const upthread_mutexattr_t* attr;
  atomic_t lock;
} upthread_mutex_t;

/* TODO: MAX_UPTHREADS is arbitrarily defined for now.
 * It indicates the maximum number of threads that can wait on  
   the same cond var/ barrier concurrently. */

#define MAX_UPTHREADS 32
typedef struct
{
  volatile int sense;
  int count;
  int nprocs;
  upthread_mutex_t pmutex;
} upthread_barrier_t;

#define WAITER_CLEARED 0
#define WAITER_WAITING 1
#define SLOT_FREE 0
#define SLOT_IN_USE 1

/* Detach state.  */
enum
{
  UPTHREAD_CREATE_JOINABLE,
#define UPTHREAD_CREATE_JOINABLE	UPTHREAD_CREATE_JOINABLE
  UPTHREAD_CREATE_DETACHED
#define UPTHREAD_CREATE_DETACHED	UPTHREAD_CREATE_DETACHED
};

// TODO: how big do we want these?  ideally, we want to be able to guard and map
// more space if we go too far.
#define UPTHREAD_STACK_PAGES 4
#define UPTHREAD_STACK_SIZE (UPTHREAD_STACK_PAGES*PGSIZE)

typedef struct
{
  int pshared;
} upthread_condattr_t;


typedef struct
{
  const upthread_condattr_t* attr;
  uint32_t waiters[MAX_UPTHREADS];
  uint32_t in_use[MAX_UPTHREADS];
  uint32_t next_waiter; //start the search for an available waiter at this spot
} upthread_cond_t;
typedef struct 
{
	size_t stacksize;
	int detachstate;
} upthread_attr_t;
typedef int upthread_barrierattr_t;
typedef uint32_t upthread_once_t;
typedef void** upthread_key_t;

/* Akaros upthread extensions / hacks */
void upthread_can_vcore_request(bool can);	/* default is TRUE */

/* Juggle extensions */
int upthread_set_sched_period(uint64_t us);

/* The upthreads API */
int upthread_attr_init(upthread_attr_t *);
int upthread_attr_destroy(upthread_attr_t *);
int upthread_create(upthread_t *, const upthread_attr_t *,
                   void *(*)(void *), void *);
int upthread_join(upthread_t, void **);
int upthread_yield(void);

int upthread_attr_setdetachstate(upthread_attr_t *__attr,int __detachstate);

int upthread_mutex_destroy(upthread_mutex_t *);
int upthread_mutex_init(upthread_mutex_t *, const upthread_mutexattr_t *);
int upthread_mutex_lock(upthread_mutex_t *);
int upthread_mutex_trylock(upthread_mutex_t *);
int upthread_mutex_unlock(upthread_mutex_t *);
int upthread_mutex_destroy(upthread_mutex_t *);

int upthread_mutexattr_init(upthread_mutexattr_t *);
int upthread_mutexattr_destroy(upthread_mutexattr_t *);
int upthread_mutexattr_gettype(const upthread_mutexattr_t *, int *);
int upthread_mutexattr_settype(upthread_mutexattr_t *, int);

int upthread_cond_init(upthread_cond_t *, const upthread_condattr_t *);
int upthread_cond_destroy(upthread_cond_t *);
int upthread_cond_broadcast(upthread_cond_t *);
int upthread_cond_signal(upthread_cond_t *);
int upthread_cond_wait(upthread_cond_t *, upthread_mutex_t *);

int upthread_condattr_init(upthread_condattr_t *);
int upthread_condattr_destroy(upthread_condattr_t *);
int upthread_condattr_setpshared(upthread_condattr_t *, int);
int upthread_condattr_getpshared(upthread_condattr_t *, int *);

#define upthread_rwlock_t upthread_mutex_t
#define upthread_rwlockattr_t upthread_mutexattr_t
#define upthread_rwlock_destroy upthread_mutex_destroy
#define upthread_rwlock_init upthread_mutex_init
#define upthread_rwlock_unlock upthread_mutex_unlock
#define upthread_rwlock_rdlock upthread_mutex_lock
#define upthread_rwlock_wrlock upthread_mutex_lock
#define upthread_rwlock_tryrdlock upthread_mutex_trylock
#define upthread_rwlock_trywrlock upthread_mutex_trylock

upthread_t upthread_self();
int upthread_equal(upthread_t t1, upthread_t t2);
void upthread_exit(void* ret);
int upthread_once(upthread_once_t* once_control, void (*init_routine)(void));

int upthread_barrier_init(upthread_barrier_t* b, const upthread_barrierattr_t* a, int count);
int upthread_barrier_wait(upthread_barrier_t* b);
int upthread_barrier_destroy(upthread_barrier_t* b);

//added for redis compile
int upthread_detach(upthread_t __th);
int upthread_attr_setstacksize(upthread_attr_t *attr, size_t stacksize);
int upthread_attr_getstacksize(const upthread_attr_t *attr, size_t *stacksize);

//added for go compile
int upthread_kill (upthread_t __threadid, int __signo);

#ifdef __cplusplus
  }
#endif

#endif
