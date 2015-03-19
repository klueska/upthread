#ifndef UPTHREAD_H
#define UPTHREAD_H

#include "upthread-types.h"
#include "upthread-common.h"
#include "futex.h"
#include "semaphore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upthread-juggle extensions / hacks */
void upthread_can_vcore_request(bool can);	/* default is TRUE */
void upthread_can_vcore_steal(bool can);	/* default is TRUE */
void upthread_set_num_vcores(int num);	/* default is max_vcores() */
void upthread_short_circuit_yield(bool ss);	/* default is TRUE */
int upthread_set_sched_period(uint64_t us);

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_H
