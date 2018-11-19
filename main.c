@@ -1,607 +0,0 @@
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <inttypes.h>

#include <sys/syscall.h>


#include "rte_ring.h"

#define RING_SIZE 4096
#define PIECE_BULK 32

#define HALF_BULK (RING_SIZE >> 1)
#define ILLEGAL_SIZE (RING_SIZE | 0x3)

/* There's even number of producer and consumer threads and each thread does
 * this many successful enq or deq operations */
#define NUM_BULK_OP ((RING_SIZE / PIECE_BULK) * 100)


static int C_THREAD  =  2;
static int P_THREAD  =  1;
static int t_process =  0;

/* create two rings: one for single thread usage scenario
 * and another for multiple thread usage scenario.
 * st - single thread usage scenario
 * mt - multiple thread usage scenario
 */
static const char *st_ring_name = "ST basic ring";
static const char *mt_ring_name = "MT basic ring";
static const char *ring_name    = "stress_ring";

static _ring_t *st_ring, *mt_ring, *r_stress;


/* dummy object pointers for enqueue and dequeue testing */
static void **test_enq_data;
static void **test_deq_data;

#define MAX_WORKERS 32 /**< Maximum number of work threads */

static int worker_results[MAX_WORKERS];


typedef struct {
	int index;
	int  ppid;
	char data[1024];
}user_data_t;

/*
 * Do not use these macros.
 */
#define __FILENAME__ \
	(strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define NO_PATH(file_name) (strrchr((file_name), '/') ? \
			    strrchr((file_name), '/') + 1 : (file_name))

/**
 * default LOG macro.
 */
#define TEST_LOG(fmt, ...) \
	do { \
			fprintf(stderr, "%s:%d:%s():" fmt, __FILENAME__, \
			__LINE__, __func__, ##__VA_ARGS__); \
	} while (0)


#define STR(x) #x

#define CU_ASSERT(x)						\
	do {							\
		if (!(x)) {					\
			printf(__FILE__ "(%d): assert failed: "	\
			       STR(x) "\n", __LINE__);		\
			exit(1);				\
		}						\
	} while (0)


/* basic test cases */
void ring_test_basic_create(void)
{
	/* prove illegal size shall fail */
	st_ring = _ring_create(st_ring_name, ILLEGAL_SIZE, 0);
	CU_ASSERT(NULL == st_ring);

	/* create ring for single thread usage scenario */
	st_ring = _ring_create(st_ring_name, RING_SIZE,
				   _RING_F_SP_ENQ | _RING_F_SC_DEQ);

	CU_ASSERT(NULL != st_ring);

	/* create ring for multiple thread usage scenario */
	mt_ring = _ring_create(mt_ring_name, RING_SIZE,
				   _RING_SHM_PROC);

	CU_ASSERT(NULL != mt_ring);
	
	TEST_LOG("ring_test_basic_create success\n");
}


static int ring_test_basic_start(void)
{
	int i = 0;

	/* alloc dummy object pointers for enqueue testing */
	test_enq_data = malloc(RING_SIZE * 2 * sizeof(void *));
	if (NULL == test_enq_data) {
		TEST_LOG("failed to allocate basic test enqeue data\n");
		return -1;
	}
	TEST_LOG("test_enq_data=0x%x, sizeof(void *)=%d \n", test_enq_data, sizeof(void *));

	for (i = 0; i < RING_SIZE * 2; i++)
	{
		test_enq_data[i] = (void *)(unsigned long)i;
		//TEST_LOG("test_enq_data[%d]=0x%x, %p \n", i,  test_enq_data[i], test_enq_data+i);
	}

	/* alloc dummy object pointers for dequeue testing */
	test_deq_data = malloc(RING_SIZE * 2 * sizeof(void *));
	if (NULL == test_deq_data) {
		TEST_LOG("failed to allocate basic test dequeue data\n");
		free(test_enq_data); test_enq_data = NULL;
		return -1;
	}

	memset(test_deq_data, 0, RING_SIZE * 2 * sizeof(void *));
	
	return 0;
}


int ring_test_stress_start(void)
{
	/* multiple thread usage scenario, thread or process sharable */
	r_stress = _ring_create(ring_name, RING_SIZE, 0);
	if (r_stress == NULL) {
		TEST_LOG("create ring failed for stress.\n");
		return -1;
	}
	TEST_LOG("ring_test_stress_start succ\n");

	return 0;
}

int ring_test_stress_end(void)
{
	_ring_destroy(r_stress);
	return 0;
}


static int ring_test_basic_end(void)
{
	_ring_destroy(st_ring);
	_ring_destroy(mt_ring);

	free(test_enq_data);
	free(test_deq_data);
	return 0;
}


/* labor functions definition */
static void __do_basic_burst(_ring_t *r)
{
	int result = 0;
	unsigned int count = 0;
	void * const *source = test_enq_data;
	void * const *dest = test_deq_data;
	void **enq = NULL, **deq = NULL;

	enq = test_enq_data; deq = test_deq_data;

	/* ring is empty */
	CU_ASSERT(1 == _ring_empty(r));

	/* enqueue 1 object */
	result = _ring_enqueue_burst(r, enq, 1);
	enq += 1;
	CU_ASSERT(1 == (result & _RING_SZ_MASK));

	/* enqueue 2 objects */
	result = _ring_enqueue_burst(r, enq, 2);
	enq += 2;
	CU_ASSERT(2 == (result & _RING_SZ_MASK));

	/* enqueue HALF_BULK objects */
	result = _ring_enqueue_burst(r, enq, HALF_BULK);
	enq += HALF_BULK;
	CU_ASSERT(HALF_BULK == (result & _RING_SZ_MASK));

	/* ring is neither empty nor full */
	CU_ASSERT(0 == _ring_full(r));
	CU_ASSERT(0 == _ring_empty(r));

	_ring_dump(r);

	/* _ring_count() equals enqueued */
	count = (1 + 2 + HALF_BULK);
	CU_ASSERT(count == _ring_count(r));
	/* _ring_free_count() equals rooms left */
	count = (RING_SIZE - 1) - count;
	CU_ASSERT(count == _ring_free_count(r));

	/* exceed the size, enquene as many as possible */
	result = _ring_enqueue_burst(r, enq, HALF_BULK);
	enq += count;
	CU_ASSERT(count == (result & _RING_SZ_MASK));
	CU_ASSERT(1 == _ring_full(r));

	/* dequeue 1 object */
	result = _ring_dequeue_burst(r, deq, 1);
	deq += 1;
	CU_ASSERT(1 == (result & _RING_SZ_MASK));

	/* dequeue 2 objects */
	result = _ring_dequeue_burst(r, deq, 2);
	deq += 2;
	CU_ASSERT(2 == (result & _RING_SZ_MASK));

	/* dequeue HALF_BULK objects */
	result = _ring_dequeue_burst(r, deq, HALF_BULK);
	deq += HALF_BULK;
	CU_ASSERT(HALF_BULK == (result & _RING_SZ_MASK));

	/* _ring_free_count() equals dequeued */
	count = (1 + 2 + HALF_BULK);
	CU_ASSERT(count == _ring_free_count(r));
	/* _ring_count() equals remained left */
	count = (RING_SIZE - 1) - count;
	CU_ASSERT(count == _ring_count(r));

	/* underrun the size, dequeue as many as possible */
	result = _ring_dequeue_burst(r, deq, HALF_BULK);
	deq += count;
	CU_ASSERT(count == (result & _RING_SZ_MASK));
	CU_ASSERT(1 == _ring_empty(r));

	/* check data */
	CU_ASSERT(0 == memcmp(source, dest, deq - dest));

	/* reset dequeue data */
	memset(test_deq_data, 0, RING_SIZE * 2 * sizeof(void *));
}

/* incomplete ring API set: strange!
 * complement _ring_enqueue/dequeue_bulk to improve coverage
 */
static inline int __ring_enqueue_bulk(
	_ring_t *r, void * const *objects, unsigned bulk)
{
	if (r->prod.sp_enqueue)
		return _ring_sp_enqueue_bulk(r, objects, bulk);
	else
		return _ring_mp_enqueue_bulk(r, objects, bulk);
}

static inline int __ring_dequeue_bulk(
	_ring_t *r, void **objects, unsigned bulk)
{
	if (r->cons.sc_dequeue)
		return _ring_sc_dequeue_bulk(r, objects, bulk);
	else
		return _ring_mc_dequeue_bulk(r, objects, bulk);
}

static void __do_basic_bulk(_ring_t *r)
{
	int result = 0;
	unsigned int count = 0;
	void * const *source = test_enq_data;
	void * const *dest = test_deq_data;
	void **enq = NULL, **deq = NULL;

	enq = test_enq_data; deq = test_deq_data;

	/* ring is empty */
	CU_ASSERT(1 == _ring_empty(r));

	/* enqueue 1 object */
	result = __ring_enqueue_bulk(r, enq, 1);
	enq += 1;
	CU_ASSERT(0 == result);

	/* enqueue 2 objects */
	result = __ring_enqueue_bulk(r, enq, 2);
	enq += 2;
	CU_ASSERT(0 == result);

	/* enqueue HALF_BULK objects */
	result = __ring_enqueue_bulk(r, enq, HALF_BULK);
	enq += HALF_BULK;
	CU_ASSERT(0 == result);

	/* ring is neither empty nor full */
	CU_ASSERT(0 == _ring_full(r));
	CU_ASSERT(0 == _ring_empty(r));

	_ring_dump(r);

	/* _ring_count() equals enqueued */
	count = (1 + 2 + HALF_BULK);
	CU_ASSERT(count == _ring_count(r));
	/* _ring_free_count() equals rooms left */
	count = (RING_SIZE - 1) - count;
	CU_ASSERT(count == _ring_free_count(r));

	/* exceed the size, enquene shall fail with -ENOBUFS */
	result = __ring_enqueue_bulk(r, enq, HALF_BULK);
	CU_ASSERT(-ENOBUFS == result);

	/* fullful the ring */
	result = __ring_enqueue_bulk(r, enq, count);
	enq += count;
	CU_ASSERT(0 == result);
	CU_ASSERT(1 == _ring_full(r));

	/* dequeue 1 object */
	result = __ring_dequeue_bulk(r, deq, 1);
	deq += 1;
	CU_ASSERT(0 == result);

	/* dequeue 2 objects */
	result = __ring_dequeue_bulk(r, deq, 2);
	deq += 2;
	CU_ASSERT(0 == result);

	/* dequeue HALF_BULK objects */
	result = __ring_dequeue_bulk(r, deq, HALF_BULK);
	deq += HALF_BULK;
	CU_ASSERT(0 == result);

	/* _ring_free_count() equals dequeued */
	count = (1 + 2 + HALF_BULK);
	CU_ASSERT(count == _ring_free_count(r));
	/* _ring_count() equals remained left */
	count = (RING_SIZE - 1) - count;
	CU_ASSERT(count == _ring_count(r));

	/* underrun the size, dequeue shall fail with -ENOENT */
	result = __ring_dequeue_bulk(r, deq, HALF_BULK);
	CU_ASSERT(-ENOENT == result);

	/* empty the queue */
	result = __ring_dequeue_bulk(r, deq, count);
	deq += count;
	CU_ASSERT(0 == result);
	CU_ASSERT(1 == _ring_empty(r));

	/* check data */
	CU_ASSERT(0 == memcmp(source, dest, deq - dest));

	/* reset dequeue data */
	memset(test_deq_data, 0, RING_SIZE * 2 * sizeof(void *));
}



static void __do_basic_watermark(_ring_t *r)
{
	int result = 0;
	void * const *source = test_enq_data;
	void * const *dest = test_deq_data;
	void **enq = NULL, **deq = NULL;

	enq = test_enq_data; deq = test_deq_data;

	/* bulk = 3/4 watermark to trigger alarm on 2nd enqueue */
	const unsigned watermark = PIECE_BULK;
	const unsigned bulk = (watermark / 4) * 3;

	/* watermark cannot exceed ring size */
	result = _ring_set_water_mark(r, ILLEGAL_SIZE);
	CU_ASSERT(-EINVAL == result);

	/* set watermark */
	result = _ring_set_water_mark(r, watermark);
	CU_ASSERT(0 == result);

	/* 1st enqueue shall succeed */
	result = __ring_enqueue_bulk(r, enq, bulk);
	enq += bulk;
	CU_ASSERT(0 == result);

	/* 2nd enqueue shall succeed but return -EDQUOT */
	result = __ring_enqueue_bulk(r, enq, bulk);
	enq += bulk;
	CU_ASSERT(-EDQUOT == result);

	_ring_dump(r);

	/* dequeue 1st bulk */
	result = __ring_dequeue_bulk(r, deq, bulk);
	deq += bulk;
	CU_ASSERT(0 == result);

	/* dequeue 2nd bulk */
	result = __ring_dequeue_bulk(r, deq, bulk);
	deq += bulk;
	CU_ASSERT(0 == result);

	/* check data */
	CU_ASSERT(0 == memcmp(source, dest, deq - dest));

	/* reset watermark */
	result = _ring_set_water_mark(r, 0);
	CU_ASSERT(0 == result);

	/* reset dequeue data */
	memset(test_deq_data, 0, RING_SIZE * 2 * sizeof(void *));
}



static void ring_test_basic_burst(void)
{
	/* two rounds to cover both single
	 * thread and multiple thread APIs
	 */
	__do_basic_burst(st_ring);
	__do_basic_burst(mt_ring);
	
	TEST_LOG("ring_test_basic_burst success\n");
}

static void ring_test_basic_bulk(void)
{
	__do_basic_bulk(st_ring);
	__do_basic_bulk(mt_ring);
	
	TEST_LOG("ring_test_basic_bulk success\n");
}

static void ring_test_basic_watermark(void)
{
	__do_basic_watermark(st_ring);
	__do_basic_watermark(mt_ring);
	
	TEST_LOG("ring_test_basic_watermark success\n");
}

/* worker function for multiple producer instances */
static void* do_producer(void *r)
{
	void *enq[PIECE_BULK];
	int i;
	int num = NUM_BULK_OP;
	TEST_LOG("do consumer num<%d>\n",num);

	/* data pattern to be evaluated later in consumer */
	for (i = 0; i < PIECE_BULK; i++)
		enq[i] = (void *)(uintptr_t)i;

	while (num)
		if (_ring_mp_enqueue_bulk(r_stress, enq, PIECE_BULK) == 0)
			num--;

	return 0;
}

/* worker function for multiple consumer instances */
static void* do_consumer(void *r)
{
	void *deq[PIECE_BULK];
	int i;
	int num = NUM_BULK_OP;

	while (num) {
		if (_ring_mc_dequeue_bulk(r_stress, deq, PIECE_BULK) == 0) {
			num--;

			/* evaluate the data pattern */
			for (i = 0; i < PIECE_BULK; i++)
				CU_ASSERT(deq[i] == (void *)(uintptr_t)i);
		}
	}

	return 0;
}

static void do_process(void)
{
	ring_test_basic_create();
	ring_test_basic_burst();
	ring_test_basic_bulk();
	ring_test_basic_watermark();
}

static void usage(char *progname)
{
	TEST_LOG("\n"
		   "Usage: %s OPTIONS\n"
		   "  E.g. %s -p 1 -c 2\n"
		   "\n"
		   "Mandatory OPTIONS:\n"
		   "  -p, --producer count\n"
		   "  -c, consumer count\n"
		   "  -t, test model, 1:unint test; >=2 stress test\n"
 		   "  -h, --help           Display help and exit.\n"
		   "\n", NO_PATH(progname), NO_PATH(progname)
		);
}

static void parse_args(int argc, char *argv[])
{
	int opt;
	int long_index;
	int i;
	static struct option longopts[] = {
		{"producer count", required_argument, NULL, 'p'},
		{"consumer count", required_argument, NULL, 'c'},	 
		{"test model", no_argument, NULL, 't'},		 
		{"help", required_argument,NULL, 'h'}, 
		{NULL, 0, NULL, 0}
	};

	while (1) {
		opt = getopt_long(argc, argv, "+p:c:t:h:", longopts, &long_index);

		if (opt == -1)
			break;	/* No more options */

		switch (opt) {
		case 'p':
			P_THREAD = atoi(optarg);
			break;
			/* parse packet-io interface names */
		case 'c':
			C_THREAD = atoi(optarg);
			break;
		case 't':
			t_process = atoi(optarg);
			break;

		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;

		default:
			break;
		}
	}

	optind = 1;		/* reset 'extern optind' from the getopt lib */
}

int main(int argc, char *argv[] )
{
	int opt, i = 0;
		
    pthread_t tid_producer[P_THREAD];
	pthread_t tid_customer[C_THREAD];

	parse_args(argc, argv);

	if(t_process >= 1)
	{
		TEST_LOG("ring_test_stress_start\n");
		
		ring_test_stress_start();
		
		for(i=0;i<P_THREAD;i++)
		{
			pthread_create(&tid_producer[i], NULL,	do_producer, NULL);
		}
		
		for(i=0;i<C_THREAD;i++)
		{
			pthread_create(&tid_customer[i], NULL,	do_consumer, NULL);
		}
		
		for(i=0;i<P_THREAD;i++)
		{
			 pthread_join(tid_producer[i], NULL);
		}
		
		for(i=0;i<C_THREAD;i++)
		{
			 pthread_join(tid_customer[i], NULL);
		}
		
		ring_test_stress_end();
	}
	else
	{
		ring_test_basic_start();
		do_process();
		ring_test_basic_end();
	}
			
	printf("**************************end main***************************\n");
	return 0;
}