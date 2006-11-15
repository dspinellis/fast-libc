/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)qsort.c	8.1 (Berkeley) 6/4/93";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
#ifndef linux
__FBSDID("$FreeBSD: src/lib/libc/stdlib/qsort.c,v 1.12 2002/09/10 02:04:49 wollman Exp $");
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#ifndef linux
#include <pmc.h>
#endif

/*
 * Defining the following macro will cause all
 * pthreads API invocation to be checked, against
 * invocation errors (e.g. trying to lock an uninitialized
 * mutex.  Other errors (e.g. unavailable resources)
 * are always checked and acted upon.
 */
#define DEBUG_API 1

/*
 * Defining the followin macro will print on stderr the results
 * of various sort phases.
 */
/* #define DEBUG_SORT 1 */

/*
 * Defining the following macro will produce logging
 * information on the algorithm's progress
 */
/* #define DEBUG_LOG 1 */

#ifdef DEBUG_API
#define ensure(x) do {if (!(x)) { perror(#x); exit(1); } } while(0)
#else /* !DEBUG_API */
#define ensure(x) (x)
#endif

#ifdef DEBUG_LOG
#define DLOG(...) fprintf(stderr, __VA_ARGS__)
#else
#define DLOG(...)
#endif

#ifdef I_AM_QSORT_R
typedef int		 cmp_t(void *, const void *, const void *);
#else
typedef int		 cmp_t(const void *, const void *);
#endif
static inline char	*med3(char *, char *, char *, cmp_t *, void *);
static inline void	 swapfunc(char *, char *, int, int);

#define min(a, b)	(a) < (b) ? a : b

/*
 * Qsort routine from Bentley & McIlroy's "Engineering a Sort Function".
 */
#define swapcode(TYPE, parmi, parmj, n) { 		\
	long i = (n) / sizeof (TYPE); 			\
	TYPE *pi = (TYPE *) (parmi); 		\
	TYPE *pj = (TYPE *) (parmj); 		\
	do { 						\
		TYPE	t = *pi;		\
		*pi++ = *pj;				\
		*pj++ = t;				\
        } while (--i > 0);				\
}


static inline void
swapfunc(a, b, n, swaptype)
	char *a, *b;
	int n, swaptype;
{
	if(swaptype <= 1)
		swapcode(long, a, b, n)
	else
		swapcode(char, a, b, n)
}

#define swap(a, b)					\
	if (swaptype == 0) {				\
		long t = *(long *)(a);			\
		*(long *)(a) = *(long *)(b);		\
		*(long *)(b) = t;			\
	} else						\
		swapfunc(a, b, es, swaptype)

#define vecswap(a, b, n) 	if ((n) > 0) swapfunc(a, b, n, swaptype)

#ifdef I_AM_QSORT_R
#define	CMP(t, x, y) (cmp((t), (x), (y)))
#else
#define	CMP(t, x, y) (cmp((x), (y)))
#endif

static inline char *
med3(char *a, char *b, char *c, cmp_t *cmp, void *thunk
#ifndef I_AM_QSORT_R
//__unused
#endif
)
{
	return CMP(thunk, a, b) < 0 ?
	       (CMP(thunk, b, c) < 0 ? b : (CMP(thunk, a, c) < 0 ? c : a ))
              :(CMP(thunk, b, c) > 0 ? b : (CMP(thunk, a, c) < 0 ? a : c ));
}

/* Variant part passed to qsort invocations. */
struct qsort {
	struct common *common;	/* Common shared elements. */
	void *a;		/* Base. */
	size_t n;		/* Number of elements. */
	pthread_t id;		/* Thread id. */
	bool used;		/* True if slot is in use. */
};

/* Invariant common part, shared across invocations. */
struct common {
	int swaptype;		/* Code to use for swapping */
	struct chunk *chunk;	/* Chunk of work. */
	size_t es;		/* Element size. */
	void *thunk;		/* Thunk for qsort_r */
	cmp_t *cmp;		/* Comparison function */
	int nslots;		/* Number of thread slots. */
	int workingslots;	/* Number of threads doing sort work. */
				/* Used for thread spawning. */
	int activeslots;	/* Number of threads active. */
				/* Used for terminating. */
	int forkelem;		/* Minimum number of elements for a new thread. */
	struct qsort *slots;	/* Slots for storing per-thread data. */
	pthread_mutex_t mtx_common;	/* For accessing used threads. */
	pthread_mutex_t mtx_slot;	/* For signalling slot availability. */
	pthread_mutex_t mtx_done;	/* For signalling termination. */
	pthread_cond_t cond_slot;	/* For signalling slot availability. */
	pthread_cond_t cond_done;	/* For signalling termination. */
	pthread_attr_t detached;	/* Detached state attribute. */
};

static void *qsort_algo(void *p);
static void qsort_launch(struct qsort *qs);

/* The multithreaded qsort public interface */
void
qsort_mt(void *a, size_t n, size_t es, cmp_t *cmp, int maxthreads, int forkelem)
{
	int ncpu;
	struct qsort qs;
	struct common c;
	int i;
	bool bailout = true;

	if (n < forkelem)
		goto f1;
	errno = 0;
#ifndef linux
	if (maxthreads == 0) {
		if (pmc_init() == 0 && (ncpu = pmc_ncpu()) != -1)
			maxthreads = ncpu;
		else
			maxthreads = 2;
	}
#endif
	/* XXX temporarily disabled for stress and performance testing.
	if (maxthreads == 1)
		goto f1;
	*/
	/* Try to initialize the resources we need. */
	if (pthread_mutex_init(&c.mtx_common, NULL) != 0)
		goto f1;
	if (pthread_mutex_init(&c.mtx_slot, NULL) != 0)
		goto f2;
	if (pthread_mutex_init(&c.mtx_done, NULL) != 0)
		goto f3;
	if (pthread_cond_init(&c.cond_slot, NULL) != 0)
		goto f4;
	if (pthread_cond_init(&c.cond_done, NULL) != 0)
		goto f5;
	if (pthread_attr_init(&c.detached) != 0)
		goto f6;
	if ((c.slots = (struct qsort *)calloc(maxthreads, sizeof(struct qsort))) ==NULL)
		goto f7;
	/* All systems go. */
	bailout = false;
	ensure(pthread_attr_setdetachstate(&c.detached, PTHREAD_CREATE_DETACHED) == 0);

	/* Initialize common elements. */
	c.swaptype = ((char *)a - (char *)0) % sizeof(long) || \
		es % sizeof(long) ? 2 : es == sizeof(long)? 0 : 1;
	c.es = es;
	c.cmp = cmp;
	c.forkelem = forkelem;
	c.nslots = maxthreads;
	c.workingslots = 0;
	c.activeslots = 0;

	/* Hand out the first work batch. */
	qs.a = a;
	qs.n = n;
	qs.common = &c;
	qsort_launch(&qs);

	/* Wait for all threads to finish */
	ensure(pthread_mutex_lock(&c.mtx_done) == 0);
	for (;;) {
		ensure(pthread_cond_wait(&c.cond_done, &c.mtx_done) == 0);
		DLOG("Got completion signal.\n");
		ensure(pthread_mutex_lock(&c.mtx_common) == 0);
		if (c.activeslots == 0)
			break;
		ensure(pthread_mutex_unlock(&c.mtx_common) == 0);
	}
	ensure(pthread_mutex_unlock(&c.mtx_done) == 0);
	ensure(pthread_mutex_unlock(&c.mtx_common) == 0);

	/* Free acquired resources. */
	free(c.slots);
f7:	ensure(pthread_attr_destroy(&c.detached) == 0);
f6:	ensure(pthread_cond_destroy(&c.cond_done) == 0);
f5:	ensure(pthread_cond_destroy(&c.cond_slot) == 0);
f4:	ensure(pthread_mutex_destroy(&c.mtx_done) == 0);
f3:	ensure(pthread_mutex_destroy(&c.mtx_slot) == 0);
f2:	ensure(pthread_mutex_destroy(&c.mtx_common) == 0);
	if (bailout) {
		DLOG("Resource initialization failed; bailing out.\n");
		/* XXX should include a syslog call here */
		fprintf(stderr, "Resource initialization failed; bailing out.\n");
f1:		qsort(a, n, es, cmp);
	}
}

#define thunk NULL

/*
 * Launch a quicksort thread.
 * The qs pointer to our thread's data is temporary and
 * may be destroyed after this routine returns.
 */
static void
qsort_launch(struct qsort *qs)
{
	int i;

	DLOG("%10s n=%-10d Start at %p\n", "Launcher", qs->n, qs->a);
#ifdef SORT_DEBUG
	for (i = 0; i < qs->n; i++)
		fprintf(stderr, "%d ", ((int*)qs->a)[i]);
	putc('\n', stderr);
#endif
	for (;;) {
		ensure(pthread_mutex_lock(&qs->common->mtx_common) == 0);
		if (qs->common->workingslots < qs->common->nslots) {
			qs->common->workingslots++;
			qs->common->activeslots++;
			for (i = 0; i < qs->common->nslots; i++)
				if (!qs->common->slots[i].used) {
					qs->common->slots[i] = *qs;
					qs = &qs->common->slots[i];
					qs->used = true;
					break;
				}
			assert(i != qs->common->nslots);
			ensure(pthread_mutex_unlock(&qs->common->mtx_common) == 0);
			if (pthread_create(&qs->id, &qs->common->detached, qsort_algo, qs) == 0) {
				DLOG("%10x n=%-10d Started new thread: i=%d activeslots=%d\n",
				    qs->id, qs->n, i, qs->common->activeslots);
				return;
			} else if (errno == EAGAIN) {
				/* Could sleep(2), but probably faster to qsort(3). */
				ensure(pthread_mutex_lock(&qs->common->mtx_common) == 0);
				qs->common->workingslots--;
				qs->common->activeslots--;
				qs->used = false;
				ensure(pthread_mutex_unlock(&qs->common->mtx_common) == 0);
				qsort(qs->a, qs->n, qs->common->es, qs->common->cmp);
				/* XXX should include a syslog call here */
				DLOG("EAGAIN on create_thread\n");
				fprintf(stderr, "EAGAIN on create_thread\n");
				return;
			} else {
				fprintf(stderr, "active=%d, working=%d\n",
				    qs->common->workingslots,
				    qs->common->activeslots);
				perror("pthread_create");
				assert(("pthread_create failed", false));
			}
		} else
			ensure(pthread_mutex_unlock(&qs->common->mtx_common) == 0);
		/* Wait for a thread to finish. */
		DLOG("%10s n=%-10d Wait for thread termination\n", "Launcher", qs->n);
		ensure(pthread_mutex_lock(&qs->common->mtx_slot) == 0);
		ensure(pthread_cond_wait(&qs->common->cond_slot,
		    &qs->common->mtx_slot) == 0);
		ensure(pthread_mutex_unlock(&qs->common->mtx_slot) == 0);
	}
}

/* Thread-callable quicksort. */
static void *
qsort_algo(void *p)
{
	char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
	int d, r, swaptype, swap_cnt;
	struct qsort *qs, left, right;
	void *a;
	size_t n, es;
	cmp_t *cmp;
	int nl, nr;
	struct common *c;
#ifdef DEBUG_LOG
	pthread_t id;
#endif

	qs = p;
#ifdef DEBUG_LOG
	id = qs->id;
#endif
	/* Initialize actual qsort arguments. */
	c = qs->common;
	a = qs->a;
	n = qs->n;
	es = c->es;
	cmp = c->cmp;
	swaptype = c->swaptype;
	DLOG("%10x n=%-10d Running thread.\n", id, n);
#ifdef SORT_DEBUG
	int i;
	for (i = 0; i < qs->n; i++)
		fprintf(stderr, "%d ", ((int*)qs->a)[i]);
	putc('\n', stderr);
#endif

	/* From here on qsort(3) business as usual. */
	swap_cnt = 0;
	if (n < 7) {
		for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
			for (pl = pm;
			     pl > (char *)a && CMP(thunk, pl - es, pl) > 0;
			     pl -= es)
				swap(pl, pl - es);
		left.n = right.n = 0;
		goto done;
	}
	pm = (char *)a + (n / 2) * es;
	if (n > 7) {
		pl = a;
		pn = (char *)a + (n - 1) * es;
		if (n > 40) {
			d = (n / 8) * es;
			pl = med3(pl, pl + d, pl + 2 * d, cmp, thunk);
			pm = med3(pm - d, pm, pm + d, cmp, thunk);
			pn = med3(pn - 2 * d, pn - d, pn, cmp, thunk);
		}
		pm = med3(pl, pm, pn, cmp, thunk);
	}
	swap(a, pm);
	pa = pb = (char *)a + es;

	pc = pd = (char *)a + (n - 1) * es;
	for (;;) {
		while (pb <= pc && (r = CMP(thunk, pb, a)) <= 0) {
			if (r == 0) {
				swap_cnt = 1;
				swap(pa, pb);
				pa += es;
			}
			pb += es;
		}
		while (pb <= pc && (r = CMP(thunk, pc, a)) >= 0) {
			if (r == 0) {
				swap_cnt = 1;
				swap(pc, pd);
				pd -= es;
			}
			pc -= es;
		}
		if (pb > pc)
			break;
		swap(pb, pc);
		swap_cnt = 1;
		pb += es;
		pc -= es;
	}
	if (swap_cnt == 0) {  /* Switch to insertion sort */
		for (pm = (char *)a + es; pm < (char *)a + n * es; pm += es)
			for (pl = pm;
			     pl > (char *)a && CMP(thunk, pl - es, pl) > 0;
			     pl -= es)
				swap(pl, pl - es);
		left.n = right.n = 0;
		goto done;
	}

	pn = (char *)a + n * es;
	r = min(pa - (char *)a, pb - pa);
	vecswap(a, pb - r, r);
	r = min(pd - pc, pn - pd - es);
	vecswap(pb, pn - r, r);

	/*
	 * Sort the partitioned subparts.
	 * First see if we should sort them without a new thread.
	 */
	left.n = (pb - pa) / es;
	if (left.n > 0 && left.n <= c->forkelem)
		qsort(a, n, es, cmp);
	right.n = (pd - pc) / es;
	if (right.n > 0 && right.n <= c->forkelem)
		qsort(a, n, es, cmp);
	/*
	 * At this point all the hard work is done.  We mark
	 * a slot as available, so that the launcher won't deadlock.
	 */
done:
	DLOG("%10x n=%-10d Hard work finished ln=%d rn=%d.\n", id, n, left.n, right.n);
	ensure(pthread_mutex_lock(&c->mtx_common) == 0);
	c->workingslots--;
	/* Free our slot, and stop using it. */
	qs->used = false;
	qs = NULL;
	/* Indicate that a slot is now free. */
	DLOG("%10x n=%-10d Signal free slot.\n", id, n);
	ensure(pthread_mutex_lock(&c->mtx_slot) == 0);
	ensure(pthread_cond_signal(&c->cond_slot) == 0);
	ensure(pthread_mutex_unlock(&c->mtx_slot) == 0);
	ensure(pthread_mutex_unlock(&c->mtx_common) == 0);
	/* Launch threads for the two halfs. */
	if (left.n > c->forkelem) {
		left.common = c;
		left.a = a;
		qsort_launch(&left);
	}
	if (right.n > c->forkelem) {
		right.common = c;
		right.a = pn - right.n * es;
		qsort_launch(&right);
	}
	DLOG("%10x n=%-10d Sub-launchers finished.\n", id, n);
	/*
	 * At this point any additional threads have been launched.
	 * If all slots are free, it means that all our work is done.
	 */
	ensure(pthread_mutex_lock(&c->mtx_common) == 0);
	c->activeslots--;
	if (c->activeslots == 0) {
		DLOG("%10x n=%-10d Signalling completion.\n", id, n);
		ensure(pthread_cond_signal(&c->cond_done) == 0);
	}
	ensure(pthread_mutex_unlock(&c->mtx_common) == 0);
	DLOG("%10x n=%-10d Finished.\n", id, n);
}

#ifdef TEST
#include <string.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/resource.h>

int
int_compare(const void *a, const void *b)
{
	return (*(uint32_t *)a - *(uint32_t *)b);
}

void *
xmalloc(size_t s)
{
	void *p;

	if ((p = malloc(s)) == NULL) {
		perror("malloc");
		exit(1);
	}
	return (p);
}

void
usage(void)
{
	fprintf(stderr, "usage: qsort_mt [-stv] [-f forkelements] [-h threads] [-n elements]\n"
		"\t-l\tRun the libc version of qsort\n"
		"\t-s\tTest with 20-byte strings, instead of integers\n"
		"\t-t\tPrint timing results\n"
		"\t-v\tVerify the integer results\n"
		"Defaults are 1e7 elements, 2 threads, 100 fork elements\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	bool opt_str = false;
	bool opt_time = false;
	bool opt_verify = false;
	bool opt_libc = false;
	int ch, i;
	size_t nelem = 10000000;
	int threads = 2;
	int forkelements = 100;
	uint32_t *int_elem;
	char *ep;
	char **str_elem;
	struct timeval start, end;
	struct rusage ru;

	while ((ch = getopt(argc, argv, "f:h:ln:stv")) != -1) {
		switch (ch) {
		case 'f':
			forkelements = (int)strtol(optarg, &ep, 10);
                        if (forkelements <= 0 || *ep != '\0') {
				warnx("illegal number, -f argument -- %s",
					optarg);
				usage();
			}
			break;
		case 'h':
			threads = (int)strtol(optarg, &ep, 10);
                        if (threads < 0 || *ep != '\0') {
				warnx("illegal number, -h argument -- %s",
					optarg);
				usage();
			}
			break;
		case 'l':
			opt_libc = true;
			break;
		case 'n':
			nelem = (size_t)strtol(optarg, &ep, 10);
                        if (nelem <= 0 || *ep != '\0') {
				warnx("illegal number, -n argument -- %s",
					optarg);
				usage();
			}
			break;
		case 's':
			opt_str = true;
			break;
		case 't':
			opt_time = true;
			break;
		case 'v':
			opt_verify = true;
			break;
		case '?':
		default:
			usage();
		}
	}

	if (opt_verify && opt_str)
		usage();

	argc -= optind;
	argv += optind;

	if (opt_str) {
		str_elem = (char **)xmalloc(nelem * sizeof(char *));
		for (i = 0; i < nelem; i++)
			if (asprintf(&str_elem[i], "%d%d", rand(), rand()) == -1) {
				perror("asprintf");
				exit(1);
			}
	} else {
		int_elem = (uint32_t *)xmalloc(nelem * sizeof(uint32_t));
		for (i = 0; i < nelem; i++)
			int_elem[i] = rand() % nelem;
	}
	gettimeofday(&start, NULL);
	if (opt_str) {
		if (opt_libc)
			qsort(str_elem, nelem, sizeof(char *), (cmp_t *)strcmp);
		else
			qsort_mt(str_elem, nelem, sizeof(char *),
			    (cmp_t *)strcmp, threads, forkelements);
	} else {
		if (opt_libc)
			qsort(int_elem, nelem, sizeof(uint32_t), int_compare);
		else
			qsort_mt(int_elem, nelem, sizeof(uint32_t), int_compare, threads, forkelements);
	}
	gettimeofday(&end, NULL);
	getrusage(RUSAGE_SELF, &ru);
#ifdef SORT_DEBUG
	for (i = 0; i < nelem; i++)
		fprintf(stderr, "%d ", int_elem[i]);
	fprintf(stderr, "\n");
#endif
	if (opt_verify)
		for (i = 1; i < nelem; i++)
			if (int_elem[i - 1] > int_elem[i]) {
				fprintf(stderr, "sort error at position %d: "
				    " %d > %d\n", i, int_elem[i - 1], int_elem[i]);
				exit(2);
			}
	if (opt_time)
		printf("%g %g %g\n",
			(end.tv_sec - start.tv_sec) +
			(end.tv_usec - start.tv_usec) / 1e6,
			ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6,
			ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6);
	return (0);
}
#endif /* TEST */
