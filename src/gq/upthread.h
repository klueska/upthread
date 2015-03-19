#ifndef UPTHREAD_H
#define UPTHREAD_H

#include "upthread-types.h"
#include "upthread-common.h"
#include "futex.h"
#include "semaphore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upthread-gq extensions / hacks */
void upthread_can_vcore_request(bool can);	/* default is TRUE */

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_H
