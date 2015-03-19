#ifndef UPTHREAD_H
#define UPTHREAD_H

#include "upthread-types.h"
#include "upthread-common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upthread-pvcq extensions / hacks */
void upthread_can_vcore_request(bool can);	/* default is TRUE */
void upthread_can_vcore_steal(bool can);	/* default is TRUE */
void upthread_set_num_vcores(int num);	/* default is max_vcores() */
void upthread_short_circuit_yield(bool ss);	/* default is TRUE */

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_H
