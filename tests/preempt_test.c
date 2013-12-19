#include <stdio.h>
#include <upthread.h> // normally installdir/headerfile
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

/* OS dependent #incs */
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <sys/time.h>

#define udelay(usec) usleep(usec)

upthread_mutex_t lock = UPTHREAD_MUTEX_INITIALIZER;
//#define printf_safe(...) {}
#define printf_safe(...) \
	upthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	upthread_mutex_unlock(&lock);

#define MAX_NR_TEST_THREADS 100000
int nr_threads = 100;
int nr_loops = 100;
int nr_vcores = 0;
int amt_fake_work = 0;

upthread_t my_threads[MAX_NR_TEST_THREADS];
void *my_retvals[MAX_NR_TEST_THREADS];

bool ready = FALSE;

void *worker_thread(void* arg)
{	
	/* Wait til all threads are created */
	while (!ready)
		cpu_relax();
	//for (int i = 0; i < nr_loops; i++) {
	printf("Entered thread: %d\n", upthread_self()->id);
	while(1);
	return NULL;
}

int main(int argc, char** argv) 
{
	/* OS dependent prep work */
	if (nr_vcores) {
		/* Only do the vcore trickery if requested */
		upthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
		vcore_request(nr_vcores - 1);		/* ghetto incremental interface */
		for (int i = 0; i < nr_vcores; i++) {
			printf("Vcore %d not mapped to a particular pcore\n", i);
		}
	}

	/* Create Threads */
	for (int i = 0; i < nr_threads; i++) {
		printf_safe("[A] About to create thread %d\n", i);
		assert(!upthread_create(&my_threads[i], NULL, &worker_thread, NULL));
	}
	ready = TRUE;			/* signal to any spinning uthreads to start */
	while(1);
} 
