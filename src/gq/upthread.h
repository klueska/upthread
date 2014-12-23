#ifndef UPTHREAD_H
#define UPTHREAD_H

#include "upthread-common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upthread-gq extensions / hacks */
void upthread_can_vcore_request(bool can);	/* default is TRUE */

#ifdef __cplusplus
}
#endif

#endif // UPTHREAD_H
