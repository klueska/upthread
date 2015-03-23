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

upthread_mutex_t lock = UPTHREAD_MUTEX_INITIALIZER(lock);
#define printd(...) {}
/*
#define printd(...) \
	printf(__VA_ARGS__); \
*/

#define MAX_NR_TEST_THREADS 100000
int nr_yield_threads = 100;
int nr_yield_loops = 100;
int nr_vcores = 0;
int amt_fake_work = 0;

upthread_t my_threads[MAX_NR_TEST_THREADS];
void *my_retvals[MAX_NR_TEST_THREADS];

bool ready = FALSE;

void *yield_thread(void* arg)
{	
	/* Wait til all threads are created */
	while (!ready)
		cpu_relax();
	for (int i = 0; i < nr_yield_loops; i++) {
		printd("[A] upthread %d %p on vcore %d, itr: %d\n", (int)(long)arg,
		       upthread_self(), vcore_id(), i);
		/* Fakes some work by spinning a bit.  Amount varies per uth/vcore,
		 * scaled by fake_work */
		if (amt_fake_work)
			udelay(amt_fake_work * ((int)(long)arg * (vcore_id() + 1)));
		upthread_yield();
		printd("[A] upthread %p returned from yield on vcore %d, itr: %d\n",
		            upthread_self(), vcore_id(), i);
	}
	return (void*)(upthread_self());
}

int main(int argc, char** argv) 
{
	struct timeval start_tv = {0};
	struct timeval end_tv = {0};
	long usec_diff;
	long nr_ctx_switches;

	if (argc > 1)
		nr_yield_threads = strtol(argv[1], 0, 10);
	if (argc > 2)
		nr_yield_loops = strtol(argv[2], 0, 10);
	if (argc > 3)
		nr_vcores = strtol(argv[3], 0, 10);
	if (argc > 4)
		amt_fake_work = strtol(argv[4], 0, 10);
	nr_yield_threads = MIN(nr_yield_threads, MAX_NR_TEST_THREADS);
	printf("Making %d threads of %d loops each, on %d vcore(s), %d work\n",
	       nr_yield_threads, nr_yield_loops, nr_vcores, amt_fake_work);

	/* OS dependent prep work */
	if (nr_vcores) {
		/* Only do the vcore trickery if requested */
		vcore_request(nr_vcores - 1);		/* ghetto incremental interface */
		for (int i = 0; i < nr_vcores; i++) {
			printf("Vcore %d not mapped to a particular pcore\n", i);
		}
	}

	/* create and join on yield */
	for (int i = 0; i < nr_yield_threads; i++) {
		printd("[A] About to create thread %d\n", i);
		assert(!upthread_create(&my_threads[i], NULL, &yield_thread, (void*)(long)i));
	}
	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");
	ready = TRUE;			/* signal to any spinning uthreads to start */
	for (int i = 0; i < nr_yield_threads; i++) {
		printd("[A] About to join on thread %d(%p)\n", i, my_threads[i]);
		upthread_join(my_threads[i], &my_retvals[i]);
		printd("[A] Successfully joined on thread %d (retval: %p)\n", i,
		            my_retvals[i]);
	}
	if (gettimeofday(&end_tv, 0))
		perror("End time error...");
	nr_ctx_switches = nr_yield_threads * nr_yield_loops;
	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);
	printf("Done: %d uthreads, %d loops, %d vcores, %d work\n",
	       nr_yield_threads, nr_yield_loops, nr_vcores, amt_fake_work);
	printf("Nr context switches: %ld\n", nr_ctx_switches);
	printf("Time to run: %ld usec\n", usec_diff);
	if (nr_vcores == 1)
		printf("Context switch latency: %d nsec\n",
		       (int)(1000LL*usec_diff / nr_ctx_switches));
	printf("Context switches / sec: %d\n\n",
	       (int)(1000000LL*nr_ctx_switches / usec_diff));
} 
