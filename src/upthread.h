#ifndef _UUPTHREAD_H
#define _UUPTHREAD_H

#include <sys/queue.h>
#include <parlib/vcore.h>
#include <parlib/uthread.h>
#include <parlib/mcs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stack stuff. */
#define UPTHREAD_STACK_PAGES 4
#define UPTHREAD_STACK_SIZE (UPTHREAD_STACK_PAGES*PGSIZE)

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
	STAILQ_ENTRY(upthread_tcb) next;
	int state;
	bool detached;
	struct upthread_tcb *joiner;			/* raced on by exit and join */
	uint32_t id;
	uint32_t stacksize;
	void *stacktop;
	void *(*start_routine)(void*);
	void *arg;
	void *retval;
} __attribute__((aligned(ARCH_CL_SIZE)));
typedef struct upthread_tcb* upthread_t;
STAILQ_HEAD(upthread_queue, upthread_tcb);

/* Akaros upthread extensions / hacks */
void upthread_can_vcore_request(bool can);	/* default is TRUE */
void upthread_can_vcore_steal(bool can);	/* default is TRUE */
void upthread_set_num_vcores(int num);	/* default is 1 */
void upthread_short_circuit_yield(bool ss);	/* default is TRUE */
int upthread_set_sched_period(uint64_t us);

/* The core upthreads API */
enum {
  UPTHREAD_CREATE_JOINABLE,
#define UPTHREAD_CREATE_JOINABLE	UPTHREAD_CREATE_JOINABLE
  UPTHREAD_CREATE_DETACHED
#define UPTHREAD_CREATE_DETACHED	UPTHREAD_CREATE_DETACHED
};

typedef struct {
	size_t stacksize;
	int detachstate;
} upthread_attr_t;

int upthread_attr_init(upthread_attr_t *);
int upthread_attr_destroy(upthread_attr_t *);
int upthread_attr_setdetachstate(upthread_attr_t *__attr, int __detachstate);
int upthread_attr_getstacksize(const upthread_attr_t *attr, size_t *stacksize);
int upthread_attr_setstacksize(upthread_attr_t *attr, size_t stacksize);
int upthread_create(upthread_t *, const upthread_attr_t *,
                   void *(*)(void *), void *);
int upthread_join(upthread_t, void **);
int upthread_yield(void);
void upthread_exit(void* ret);
int upthread_detach(upthread_t __th);
upthread_t upthread_self();

void __upthread_generic_yield(struct upthread_tcb *upthread);

/* Upthread mutexes */
enum {
	UPTHREAD_MUTEX_NORMAL,
	UPTHREAD_MUTEX_RECURSIVE,
	NUM_UPTHREAD_MUTEX_TYPES,
};
#define UPTHREAD_MUTEX_DEFAULT UPTHREAD_MUTEX_NORMAL

typedef struct upthread_mutexattr {
	int type;
} upthread_mutexattr_t;

typedef struct upthread_mutex {
	upthread_mutexattr_t attr;
	struct upthread_queue queue;
	mcs_lock_t lock;
	mcs_lock_qnode_t *qnode;
	int locked;
	upthread_t owner;
} upthread_mutex_t;
#define UPTHREAD_MUTEX_INITIALIZER {{0}, {0}, {0}, NULL, 0, NULL}

int upthread_mutexattr_init(upthread_mutexattr_t *);
int upthread_mutexattr_destroy(upthread_mutexattr_t *);
int upthread_mutexattr_gettype(const upthread_mutexattr_t *, int *);
int upthread_mutexattr_settype(upthread_mutexattr_t *, int);
int upthread_mutex_init(upthread_mutex_t *, const upthread_mutexattr_t *);
int upthread_mutex_trylock(upthread_mutex_t *);
int upthread_mutex_lock(upthread_mutex_t *);
int upthread_mutex_unlock(upthread_mutex_t *);
int upthread_mutex_destroy(upthread_mutex_t *);

/* Upthread condvars */
typedef struct upthread_condvar {
	mcs_lock_t lock;
	mcs_lock_qnode_t *waiting_qnode;
	upthread_mutex_t *waiting_mutex;
	struct upthread_queue queue;
} upthread_cond_t;
#define UPTHREAD_CONDVAR_INITIALIZER {{0}, NULL, NULL, {0}}
typedef void upthread_condattr_t;

int upthread_cond_init(upthread_cond_t *, const upthread_condattr_t *);
int upthread_cond_destroy(upthread_cond_t *);
int upthread_cond_broadcast(upthread_cond_t *);
int upthread_cond_signal(upthread_cond_t *);
int upthread_cond_wait(upthread_cond_t *, upthread_mutex_t *);

/* Upthread barriers */
typedef union {
	bool val;
	uint8_t padding[ARCH_CL_SIZE];
} padded_bool_t;

typedef struct {
	upthread_t *queue;
	int len;
	mcs_lock_t mtx;
	mcs_lock_qnode_t *qnode;
	int maxlen;
} contextq_t;

typedef struct upthread_barrier {
	int N;
	int arrived;
	bool wait;
	padded_bool_t *signals;
	contextq_t blocked[2];
} upthread_barrier_t;
typedef void upthread_barrierattr_t;

int upthread_barrierattr_init(upthread_barrierattr_t *attr);
int upthread_barrierattr_destroy(upthread_barrierattr_t *attr);
int upthread_barrier_init(upthread_barrier_t* b,
                          const upthread_barrierattr_t* a, int count);
int upthread_barrier_wait(upthread_barrier_t* b);
int upthread_barrier_destroy(upthread_barrier_t* b);

#ifdef __cplusplus
}
#endif

#endif
