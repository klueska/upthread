#include "upthread.h"

int upthread_key_create(upthread_key_t *__key,
                        void (*__destr_function) (void *))
{
	*__key = (upthread_key_t) dtls_key_create(__destr_function);
	return 0;
}

int upthread_key_delete(upthread_key_t __key)
{
	dtls_key_delete((dtls_key_t)__key);
	return 0;
}

void *upthread_getspecific(upthread_key_t __key)
{
	return get_dtls((dtls_key_t)__key);
}

int upthread_setspecific(upthread_key_t __key, const void *__pointer)
{
	set_dtls((dtls_key_t)__key, (void *)__pointer);
	return 0;
}

