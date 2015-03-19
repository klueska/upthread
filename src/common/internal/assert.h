#ifndef UPTHREAD_INTERNAL_ASSERT_H
#define UPTHREAD_INTERNAL_ASSERT_H

#include <assert.h>

//#define UPTHREAD_DEBUG
#ifndef UPTHREAD_DEBUG
# undef assert
# define assert(x) (x)
#endif

#endif
