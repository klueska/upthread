#include <sys/queue.h>
#include <parlib/waitfreelist.h>
#include <stdio.h>
#include <errno.h>
#include <parlib/spinlock.h>
#include "internal/assert.h"
#include "upthread.h"
#include "futex.h"

struct futex_list;

struct futex_element {
  STAILQ_ENTRY(futex_element) next;
  upthread_t upthread;
  struct futex_list *list;
  int val;
};

STAILQ_HEAD(futex_tailq, futex_element);

struct futex_list {
  struct futex_tailq tailq;
  int *uaddr;
  spin_pdr_lock_t lock;
};

/* A list of futex blocking queues, one per uaddr. */
static struct wfl futex_lists = WFL_INITIALIZER(futex_lists);
static spin_pdr_lock_t futex_lists_lock = SPINLOCK_INITIALIZER;

/* Find or create the blocking queue that corresponds to the uaddr. */
static struct futex_list *get_futex_list(int *uaddr)
{
  struct futex_list *list;
  wfl_foreach_unsafe(list, &futex_lists) {
    if (list->uaddr == uaddr)
      return list;
  }

  spin_pdr_lock(&futex_lists_lock);
    wfl_foreach_unsafe(list, &futex_lists) {
      if (list->uaddr == uaddr)
        break;
    }
    if (list == NULL) {
      list = malloc(sizeof(struct futex_list));
      if (list == NULL)
        abort();
      STAILQ_INIT(&list->tailq);
      list->uaddr = uaddr;
      spin_pdr_init(&list->lock);
      wfl_insert(&futex_lists, list);
    }
  spin_pdr_unlock(&futex_lists_lock);

  return list;
}

/* callback.  Atomically checks uaddr == val and blocks. */
static void __futex_block(struct uthread *uthread, void *arg) {
  upthread_t upthread = (upthread_t)uthread;
  struct futex_element *e = arg;
  bool block = true;

  spin_pdr_lock(&e->list->lock);
    if (*e->list->uaddr == e->val) {
      e->upthread = upthread;
      uthread_has_blocked(uthread, UTH_EXT_BLK_MUTEX);
      STAILQ_INSERT_TAIL(&e->list->tailq, e, next);
    } else {
      block = false;
    }
  spin_pdr_unlock(&e->list->lock);

  if (!block)
	uthread_paused(uthread);
}

int futex_wait(int *uaddr, int val)
{
  if (*uaddr == val) {
    struct futex_element e;
    e.list = get_futex_list(uaddr);
    e.val = val;
    uthread_yield(true, __futex_block, &e);
  }
  return 0;
}

int futex_wake_one(int *uaddr)
{
  struct futex_list *list = get_futex_list(uaddr);
  spin_pdr_lock(&list->lock);
    struct futex_element *e = STAILQ_FIRST(&list->tailq);
    if (e != NULL)
      STAILQ_REMOVE_HEAD(&list->tailq, next);
  spin_pdr_unlock(&list->lock);

  if (e != NULL) {
    uthread_runnable((struct uthread*)e->upthread);
    return 1;
  }
  return 0;
}

static inline int unblock_futex_queue(struct futex_tailq *q)
{
  int num = 0;
  struct futex_element *e,*n;
  for (e = STAILQ_FIRST(q), num = 0; e != NULL; e = n, num++) {
    n = STAILQ_NEXT(e, next);
    uthread_runnable((struct uthread*)e->upthread);
  }

  return num;
}

int futex_wake_all(int *uaddr)
{
  struct futex_list *list = get_futex_list(uaddr);

  spin_pdr_lock(&list->lock);
    struct futex_tailq q = list->tailq;
    STAILQ_INIT(&list->tailq);
  spin_pdr_unlock(&list->lock);

  return unblock_futex_queue(&q);
}

int futex_wake_some(int *uaddr, int count)
{
  struct futex_tailq q = STAILQ_HEAD_INITIALIZER(q);
  struct futex_list *list = get_futex_list(uaddr);
  struct futex_element *e;

  spin_pdr_lock(&list->lock);
    /* Remove up to count entries from the queue, and remeber them locally. */
    while (count-- > 0 && (e = STAILQ_FIRST(&list->tailq)) != NULL) {
      STAILQ_REMOVE_HEAD(&list->tailq, next);
      STAILQ_INSERT_TAIL(&q, e, next);
    }
  spin_pdr_unlock(&list->lock);

  return unblock_futex_queue(&q);
}

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
                 int *uaddr2, int val3)
{
  assert(timeout == NULL);
  assert(uaddr2 == NULL);
  assert(val3 == 0);

  switch(op) {
    case FUTEX_WAIT:
      return futex_wait(uaddr, val);
    case FUTEX_WAKE:
      return futex_wake_some(uaddr, val);
    default:
      errno = ENOSYS;
      return -1;
  }
  return -1;
}


