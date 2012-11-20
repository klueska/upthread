#include <stdio.h>
#include <upthread.h>
#include <stdlib.h>
#include <unistd.h>

/* OS dependent #incs */
#include <parlib/parlib.h>
#include <parlib/vcore.h>
#include <sys/time.h>

#define udelay(usec) usleep(usec)

upthread_mutex_t lock = UPTHREAD_MUTEX_INITIALIZER;
#define printf_safe(...) {}
//#define printf_safe(...) \
	upthread_mutex_lock(&lock); \
	printf(__VA_ARGS__); \
	upthread_mutex_unlock(&lock);

#define MAX_NR_TEST_THREADS 100000
struct {
  char os[20];
  char platform[20];
  char thread_model[20];
  int nr_threads;
  int nr_loops;
  int nr_vcores;
  int amt_work;
} params = {"akaros", "uthread", "i686", 100, 100, 0, 0};

upthread_t my_threads[MAX_NR_TEST_THREADS];
void *my_retvals[MAX_NR_TEST_THREADS];

bool ready = FALSE;

void *yield_thread(void* arg)
{	
	/* Wait til all threads are created */
	while (!ready)
		cpu_relax();
	for (int i = 0; i < params.nr_loops; i++) {
		printf_safe("[A] upthread %d %p on vcore %d, itr: %d\n", upthread_self()->id,
		       upthread_self(), vcore_id(), i);
		/* Fakes some work by spinning a bit.  Amount varies per uth/vcore,
		 * scaled by fake_work */
		if (params.amt_work)
			udelay(params.amt_work * (upthread_self()->id * (vcore_id() + 1)));
		upthread_yield();
		printf_safe("[A] upthread %p returned from yield on vcore %d, itr: %d\n",
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
		params.nr_threads = strtol(argv[1], 0, 10);
	if (argc > 2)
		params.nr_loops = strtol(argv[2], 0, 10);
	if (argc > 3)
		params.nr_vcores = strtol(argv[3], 0, 10);
	if (argc > 4)
		params.amt_work = strtol(argv[4], 0, 10);
	if (argc > 5)
		sprintf(params.os, "%s",argv[5]);
	if (argc > 6)
		sprintf(params.platform, "%s",argv[6]);
	if (argc > 7)
		sprintf(params.thread_model, "%s",argv[7]);
	params.nr_threads = MIN(params.nr_threads, MAX_NR_TEST_THREADS);
#ifdef VERBOSE
	printf("Making %d threads of %d loops each, on %d vcore(s), %d work\n",
	       params.nr_threads, params.nr_loops, params.nr_vcores, params.amt_work);
#endif

	/* Threading model dependent prep work */
	if (params.nr_vcores) {
		/* Only do the vcore trickery if requested */
		upthread_can_vcore_request(FALSE);		/* 2LS won't manage vcores */
		upthread_lib_init();					/* gives us one vcore */
		vcore_request(params.nr_vcores - 1);	/* ghetto incremental interface */
	}

	/* create and join on yield */
	for (int i = 0; i < params.nr_threads; i++) {
		printf_safe("[A] About to create thread %d\n", i);
		assert(!upthread_create(&my_threads[i], NULL, &yield_thread, NULL));
	}
	if (gettimeofday(&start_tv, 0))
		perror("Start time error...");
	ready = TRUE;			/* signal to any spinning uthreads to start */
	for (int i = 0; i < params.nr_threads; i++) {
		printf_safe("[A] About to join on thread %d(%p)\n", i, my_threads[i]);
		upthread_join(my_threads[i], &my_retvals[i]);
		printf_safe("[A] Successfully joined on thread %d (retval: %p)\n", i,
		            my_retvals[i]);
	}
	if (gettimeofday(&end_tv, 0))
		perror("End time error...");
	nr_ctx_switches = params.nr_threads * params.nr_loops;
	usec_diff = (end_tv.tv_sec - start_tv.tv_sec) * 1000000 +
	            (end_tv.tv_usec - start_tv.tv_usec);

   /* Print the output */
	printf("BENCHMARK:%s:%s:%s:%d:%d:%d:%d:%ld\n", 
            params.os, params.platform,
            params.thread_model, params.nr_threads,
            params.nr_loops, params.nr_vcores,
            params.amt_work, usec_diff);
	printf("Done: %d uthreads, %d loops, %d vcores, %d work\n",
	       params.nr_threads, params.nr_loops, params.nr_vcores, params.amt_work);
	printf("Nr context switches: %ld\n", nr_ctx_switches);
	printf("Time to run: %ld usec\n", usec_diff);
	if (params.nr_vcores == 1)
		printf("Context switch latency: %f usec\n",
		       (float)usec_diff / (float)nr_ctx_switches);
	printf("Context switches / sec: %f\n\n",
	       ((float)nr_ctx_switches / (float)usec_diff) * 1000000);
} 
