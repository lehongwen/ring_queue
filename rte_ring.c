@@ -1,598 +0,0 @@
/* Copyright (c) 2014-2018, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Derived from FreeBSD's bufring.c
 *
 **************************************************************************
 *
 * Copyright (c) 2007,2008 Kip Macy kmacy@freebsd.org
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. The name of Kip Macy nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/

#include "rte_ring.h"


static inline void odp_cpu_pause(void)
{
	__asm__ __volatile__ ("pause");
}

/*
 * the enqueue of pointers on the ring.
 */
#define ENQUEUE_PTRS() do { \
	const uint32_t size = r->prod.size; \
	uint32_t idx = prod_head & mask; \
	if (odp_likely(idx + n < size)) { \
		for (i = 0; i < (n & ((~(unsigned)0x3))); i += 4, idx += 4) { \
			r->ring[idx] = obj_table[i]; \
			r->ring[idx + 1] = obj_table[i + 1]; \
			r->ring[idx + 2] = obj_table[i + 2]; \
			r->ring[idx + 3] = obj_table[i + 3]; \
		} \
		switch (n & 0x3) { \
		case 3: \
		r->ring[idx++] = obj_table[i++]; \
		/* fallthrough */ \
		case 2: \
		r->ring[idx++] = obj_table[i++]; \
		/* fallthrough */ \
		case 1: \
		r->ring[idx++] = obj_table[i++]; \
		} \
	} else { \
		for (i = 0; idx < size; i++, idx++)\
			r->ring[idx] = obj_table[i]; \
		for (idx = 0; i < n; i++, idx++) \
			r->ring[idx] = obj_table[i]; \
	} \
} while (0)

/*
 * the actual copy of pointers on the ring to obj_table.
 */
#define DEQUEUE_PTRS() do { \
	uint32_t idx = cons_head & mask; \
	const uint32_t size = r->cons.size; \
	if (odp_likely(idx + n < size)) { \
		for (i = 0; i < (n & (~(unsigned)0x3)); i += 4, idx += 4) {\
			obj_table[i] = r->ring[idx]; \
			obj_table[i + 1] = r->ring[idx + 1]; \
			obj_table[i + 2] = r->ring[idx + 2]; \
			obj_table[i + 3] = r->ring[idx + 3]; \
		} \
		switch (n & 0x3) { \
		case 3: \
		obj_table[i++] = r->ring[idx++]; \
		/* fallthrough */ \
		case 2: \
		obj_table[i++] = r->ring[idx++]; \
		/* fallthrough */ \
		case 1: \
		obj_table[i++] = r->ring[idx++]; \
		} \
	} else { \
		for (i = 0; idx < size; i++, idx++) \
			obj_table[i] = r->ring[idx]; \
		for (idx = 0; i < n; i++, idx++) \
			obj_table[i] = r->ring[idx]; \
	} \
} while (0)


/* create the ring */
_ring_t *
_ring_create(const char *name, unsigned count, unsigned flags)
{
	_ring_t *r;
	size_t ring_size;
	printf("_ring_create name<%s>,count<%d>\n", name,count);

	/* count must be a power of 2 */
	if (!RING_VAL_IS_POWER_2(count) || (count > _RING_SZ_MASK)) {
		printf("Requested size is invalid, must be power of 2,"
			"and do not exceed the size limit %u\n",
			_RING_SZ_MASK);

		return NULL;
	}

	ring_size = sizeof(_ring_t) +  count * sizeof(void *);
	printf("_ring_create count<%d> \n", count);

	r = (_ring_t*)malloc(ring_size);
	printf("_ring_create ring_size<%d> \n", ring_size);

	if (r != NULL) {

		memset(r, 0,ring_size );
		
		/* init the ring structure */
		snprintf(r->name, sizeof(r->name), "%s", name);
		r->flags = flags;
		r->prod.watermark = count;
		r->prod.sp_enqueue = !!(flags & _RING_F_SP_ENQ);
		r->cons.sc_dequeue = !!(flags & _RING_F_SC_DEQ);
		r->prod.size = count;
		r->cons.size = count;
		r->prod.mask = count - 1;
		r->cons.mask = count - 1;
		r->prod.head = 0;
		r->cons.head = 0;
		r->prod.tail = 0;
		r->cons.tail = 0;

	} else {
		printf("Cannot reserve memory\n");
		return NULL;
	}

	return r;
}

int _ring_destroy(_ring_t *r)
{
	free(r);
	return 0;
}

/*
 * change the high water mark. If *count* is 0, water marking is
 * disabled
 */
int _ring_set_water_mark(_ring_t *r, unsigned count)
{
	if (count >= r->prod.size)
		return -EINVAL;

	/* if count is 0, disable the watermarking */
	if (count == 0)
		count = r->prod.size;

	r->prod.watermark = count;
	return 0;
}

/**
 * Enqueue several objects on the ring (multi-producers safe).
 */
int ___ring_mp_do_enqueue(_ring_t *r, void * const *obj_table,
			  unsigned n, enum _ring_queue_behavior behavior)
{
	uint32_t prod_head, prod_next;
	uint32_t cons_tail, free_entries;
	const unsigned max = n;
	int success;
	unsigned i;
	uint32_t mask = r->prod.mask;
	int ret;

	/* move prod.head atomically */
	do {
		/* Reset n to the initial burst count */
		n = max;

		prod_head = __atomic_load_n(&r->prod.head, __ATOMIC_RELAXED);
		cons_tail = __atomic_load_n(&r->cons.tail, __ATOMIC_ACQUIRE);
		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * prod_head > cons_tail). So 'free_entries' is always between 0
		 * and size(ring)-1. */
		free_entries = (mask + cons_tail - prod_head);

		/* check that we have enough room in ring */
		if (odp_unlikely(n > free_entries)) {
			if (behavior == _RING_QUEUE_FIXED)
				return -ENOBUFS;
			/* No free entry available */
			if (odp_unlikely(free_entries == 0))
				return 0;

			n = free_entries;
		}

		prod_next = prod_head + n;
		success = __atomic_compare_exchange_n(&r->prod.head,
						      &prod_head,
						      prod_next,
						      false/*strong*/,
						      __ATOMIC_ACQUIRE,
						      __ATOMIC_RELAXED);
	} while (odp_unlikely(success == 0));

	/* write entries in ring */
	ENQUEUE_PTRS();

	/* if we exceed the watermark */
	if (odp_unlikely(((mask + 1) - free_entries + n) > r->prod.watermark)) {
		ret = (behavior == _RING_QUEUE_FIXED) ? -EDQUOT :
				(int)(n | _RING_QUOT_EXCEED);
	} else {
		ret = (behavior == _RING_QUEUE_FIXED) ? 0 : n;
	}

	/*
	 * If there are other enqueues in progress that preceded us,
	 * we need to wait for them to complete
	 */
	while (odp_unlikely(__atomic_load_n(&r->prod.tail, __ATOMIC_RELAXED) !=
			    prod_head))
		odp_cpu_pause();

	/* Release our entries and the memory they refer to */
	__atomic_store_n(&r->prod.tail, prod_next, __ATOMIC_RELEASE);
	return ret;
}

/**
 * Enqueue several objects on a ring (NOT multi-producers safe).
 */
int ___ring_sp_do_enqueue(_ring_t *r, void * const *obj_table,
			  unsigned n, enum _ring_queue_behavior behavior)
{
	uint32_t prod_head, cons_tail;
	uint32_t prod_next, free_entries;
	unsigned i;
	uint32_t mask = r->prod.mask;
	int ret;

	prod_head = r->prod.head;
	cons_tail = __atomic_load_n(&r->cons.tail, __ATOMIC_ACQUIRE);
	/* The subtraction is done between two unsigned 32bits value
	 * (the result is always modulo 32 bits even if we have
	 * prod_head > cons_tail). So 'free_entries' is always between 0
	 * and size(ring)-1. */
	free_entries = mask + cons_tail - prod_head;

	/* check that we have enough room in ring */
	if (odp_unlikely(n > free_entries)) {
		if (behavior == _RING_QUEUE_FIXED)
			return -ENOBUFS;
		/* No free entry available */
		if (odp_unlikely(free_entries == 0))
			return 0;

		n = free_entries;
	}

	prod_next = prod_head + n;
	r->prod.head = prod_next;

	/* write entries in ring */
	ENQUEUE_PTRS();

	/* if we exceed the watermark */
	if (odp_unlikely(((mask + 1) - free_entries + n) > r->prod.watermark)) {
		ret = (behavior == _RING_QUEUE_FIXED) ? -EDQUOT :
			(int)(n | _RING_QUOT_EXCEED);
	} else {
		ret = (behavior == _RING_QUEUE_FIXED) ? 0 : n;
	}

	/* Release our entries and the memory they refer to */
	__atomic_store_n(&r->prod.tail, prod_next, __ATOMIC_RELEASE);
	return ret;
}

/**
 * Dequeue several objects from a ring (multi-consumers safe).
 */

int ___ring_mc_do_dequeue(_ring_t *r, void **obj_table,
			  unsigned n, enum _ring_queue_behavior behavior)
{
	uint32_t cons_head, prod_tail;
	uint32_t cons_next, entries;
	const unsigned max = n;
	int success;
	unsigned i;
	uint32_t mask = r->prod.mask;

	/* move cons.head atomically */
	do {
		/* Restore n as it may change every loop */
		n = max;

		cons_head = __atomic_load_n(&r->cons.head, __ATOMIC_RELAXED);
		prod_tail = __atomic_load_n(&r->prod.tail, __ATOMIC_ACQUIRE);
		/* The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * cons_head > prod_tail). So 'entries' is always between 0
		 * and size(ring)-1. */
		entries = (prod_tail - cons_head);

		/* Set the actual entries for dequeue */
		if (n > entries) {
			if (behavior == _RING_QUEUE_FIXED)
				return -ENOENT;
			if (odp_unlikely(entries == 0))
				return 0;

			n = entries;
		}

		cons_next = cons_head + n;
		success = __atomic_compare_exchange_n(&r->cons.head,
						      &cons_head,
						      cons_next,
						      false/*strong*/,
						      __ATOMIC_ACQUIRE,
						      __ATOMIC_RELAXED);
	} while (odp_unlikely(success == 0));

	/* copy in table */
	DEQUEUE_PTRS();

	/*
	 * If there are other dequeues in progress that preceded us,
	 * we need to wait for them to complete
	 */
	while (odp_unlikely(__atomic_load_n(&r->cons.tail, __ATOMIC_RELAXED) !=
					    cons_head))
		odp_cpu_pause();

	/* Release our entries and the memory they refer to */
	__atomic_store_n(&r->cons.tail, cons_next, __ATOMIC_RELEASE);

	return behavior == _RING_QUEUE_FIXED ? 0 : n;
}

/**
 * Dequeue several objects from a ring (NOT multi-consumers safe).
 */
int ___ring_sc_do_dequeue(_ring_t *r, void **obj_table,
			  unsigned n, enum _ring_queue_behavior behavior)
{
	uint32_t cons_head, prod_tail;
	uint32_t cons_next, entries;
	unsigned i;
	uint32_t mask = r->prod.mask;

	cons_head = r->cons.head;
	prod_tail = __atomic_load_n(&r->prod.tail, __ATOMIC_ACQUIRE);
	/* The subtraction is done between two unsigned 32bits value
	 * (the result is always modulo 32 bits even if we have
	 * cons_head > prod_tail). So 'entries' is always between 0
	 * and size(ring)-1. */
	entries = prod_tail - cons_head;

	if (n > entries) {
		if (behavior == _RING_QUEUE_FIXED)
			return -ENOENT;
		if (odp_unlikely(entries == 0))
			return 0;

		n = entries;
	}

	cons_next = cons_head + n;
	r->cons.head = cons_next;

	/* Acquire the pointers and the memory they refer to */
	/* copy in table */
	DEQUEUE_PTRS();

	__atomic_store_n(&r->cons.tail, cons_next, __ATOMIC_RELEASE);
	return behavior == _RING_QUEUE_FIXED ? 0 : n;
}

/**
 * Enqueue several objects on the ring (multi-producers safe).
 */
int _ring_mp_enqueue_bulk(_ring_t *r, void * const *obj_table,
			  unsigned n)
{
	return ___ring_mp_do_enqueue(r, obj_table, n,
					 _RING_QUEUE_FIXED);
}

/**
 * Enqueue several objects on a ring (NOT multi-producers safe).
 */
int _ring_sp_enqueue_bulk(_ring_t *r, void * const *obj_table,
			  unsigned n)
{
	return ___ring_sp_do_enqueue(r, obj_table, n,
					 _RING_QUEUE_FIXED);
}

/**
 * Dequeue several objects from a ring (multi-consumers safe).
 */
int _ring_mc_dequeue_bulk(_ring_t *r, void **obj_table, unsigned n)
{
	return ___ring_mc_do_dequeue(r, obj_table, n,
					 _RING_QUEUE_FIXED);
}

/**
 * Dequeue several objects from a ring (NOT multi-consumers safe).
 */
int _ring_sc_dequeue_bulk(_ring_t *r, void **obj_table, unsigned n)
{
	return ___ring_sc_do_dequeue(r, obj_table, n,
					 _RING_QUEUE_FIXED);
}

/**
 * Test if a ring is full.
 */
int _ring_full(const _ring_t *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;

	return (((cons_tail - prod_tail - 1) & r->prod.mask) == 0);
}

/**
 * Test if a ring is empty.
 */
int _ring_empty(const _ring_t *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;

	return !!(cons_tail == prod_tail);
}

/**
 * Return the number of entries in a ring.
 */
unsigned _ring_count(const _ring_t *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;

	return (prod_tail - cons_tail) & r->prod.mask;
}

/**
 * Return the number of free entries in a ring.
 */
unsigned _ring_free_count(const _ring_t *r)
{
	uint32_t prod_tail = r->prod.tail;
	uint32_t cons_tail = r->cons.tail;

	return (cons_tail - prod_tail - 1) & r->prod.mask;
}

/* dump the status of the ring on the console */
void _ring_dump(const _ring_t *r)
{
	printf("ring <%s>@%p\n", r->name, r);
	printf("  flags=%x\n", r->flags);
	printf("  size=%" PRIu32 "\n", r->prod.size);
	printf("  ct=%" PRIu32 "\n", r->cons.tail);
	printf("  ch=%" PRIu32 "\n", r->cons.head);
	printf("  pt=%" PRIu32 "\n", r->prod.tail);
	printf("  ph=%" PRIu32 "\n", r->prod.head);
	printf("  used=%u\n", _ring_count(r));
	printf("  avail=%u\n", _ring_free_count(r));
	if (r->prod.watermark == r->prod.size)
		printf("  watermark=0\n");
	else
		printf("  watermark=%" PRIu32 "\n", r->prod.watermark);
}

/**
 * Enqueue several objects on the ring (multi-producers safe).
 */
int _ring_mp_enqueue_burst(_ring_t *r, void * const *obj_table,
			   unsigned n)
{
	return ___ring_mp_do_enqueue(r, obj_table, n,
					 _RING_QUEUE_VARIABLE);
}

/**
 * Enqueue several objects on a ring (NOT multi-producers safe).
 */
int _ring_sp_enqueue_burst(_ring_t *r, void * const *obj_table,
			   unsigned n)
{
	return ___ring_sp_do_enqueue(r, obj_table, n,
					_RING_QUEUE_VARIABLE);
}

/**
 * Enqueue several objects on a ring.
 */
int _ring_enqueue_burst(_ring_t *r, void * const *obj_table,
			unsigned n)
{
	if (r->prod.sp_enqueue)
		return _ring_sp_enqueue_burst(r, obj_table, n);
	else
		return _ring_mp_enqueue_burst(r, obj_table, n);
}

/**
 * Dequeue several objects from a ring (multi-consumers safe).
 */
int _ring_mc_dequeue_burst(_ring_t *r, void **obj_table, unsigned n)
{
	return ___ring_mc_do_dequeue(r, obj_table, n,
					_RING_QUEUE_VARIABLE);
}

/**
 * Dequeue several objects from a ring (NOT multi-consumers safe).
 */
int _ring_sc_dequeue_burst(_ring_t *r, void **obj_table, unsigned n)
{
	return ___ring_sc_do_dequeue(r, obj_table, n,
					 _RING_QUEUE_VARIABLE);
}

/**
 * Dequeue multiple objects from a ring up to a maximum number.
 */
int _ring_dequeue_burst(_ring_t *r, void **obj_table, unsigned n)
{
	if (r->cons.sc_dequeue)
		return _ring_sc_dequeue_burst(r, obj_table, n);
	else
		return _ring_mc_dequeue_burst(r, obj_table, n);
}