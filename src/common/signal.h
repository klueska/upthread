/* Copyright (c) 2015 The Regents of the University of California
 * Kevin Klues <klueska@cs.berkeley.edu>
 * See COPYING for details.
 */

/**
 * Interface of upthread signals
 */

#ifndef UPTHREAD_SIGNAL_H
#define UPTHREAD_SIGNAL_H

#include <sys/types.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Set a sigmask. */
int upthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_SIGNAL_H
