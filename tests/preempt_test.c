#include <stdio.h>
#include <upthread.h> // normally installdir/headerfile
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

/* OS dependent #incs */
#include <sys/time.h>
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <parlib/timing.h>

#define printd(...) \
{ \
	upthread_disable_interrupts(); \
	printf(__VA_ARGS__); \
	upthread_enable_interrupts(); \
}

#define MAX_NR_TEST_THREADS 100000
int nr_threads = 1000;
int nr_vcores = 24;
int amt_fake_work = 0;

upthread_t my_threads[MAX_NR_TEST_THREADS];
void *my_retvals[MAX_NR_TEST_THREADS];

bool ready = FALSE;

void *worker_thread(void* arg)
{	
	int id = (int)(long)arg;
	long long nr_loops = 0;
	/* Wait til all threads are created */
	printd("Entered thread: %d, vcore: %d\n", id, vcore_id());
	while (!ready)
		cpu_relax();

	while(1) {
		if ((nr_loops++ % 100000000LL) == 0)
			printd("Looping on thread: %d, vcore: %d, c: %lld\n", id, vcore_id(), nr_loops);
	}
	return NULL;
}

int main(int argc, char** argv) 
{
	upthread_set_sched_period(1000000);
	/* OS dependent prep work */
	if (nr_vcores) {
		/* Only do the vcore trickery if requested */
		upthread_can_vcore_request(FALSE);	/* 2LS won't manage vcores */
		upthread_set_num_vcores(nr_vcores);
		vcore_request(nr_vcores - 1);		/* ghetto incremental interface */
		for (int i = 0; i < nr_vcores; i++) {
			printd("Vcore %d not mapped to a particular pcore\n", i);
		}
	}

	/* Create Threads */
	for (int i = 0; i < nr_threads; i++) {
		printd("[A] About to create thread %d\n", i);
		assert(!upthread_create(&my_threads[i], NULL,
		                        &worker_thread, (void*)(long)i));
	}
	ready = TRUE;			/* signal to any spinning uthreads to start */
	while(1);
} 
