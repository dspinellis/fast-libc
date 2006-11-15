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
#include <stdio.h>
#include <string.h>
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
#define verify(x) do {				\
	int e;					\
	if ((e = x) != 0) {			\
		fprintf(stderr, "%s: %s\n", 	\
		    #x, strerror(e)); 		\
		exit(1);			\
	}					\
} while(0)
#else /* !DEBUG_API */
#define verify(x) (x)
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

/*
 * We use some elaborate condition variables and signalling
 * to ensure a bound of the number of active threads at
 * 2 * maxthreads and the size of the thread data structure
 * to maxthreads.
 */

/* Condition of starting a new thread. */
enum start_cond {
	sc_join,		/* Join thread joinid. */
	sc_signal,		/* Wait on cond_ga. */
	sc_none			/* Start immediately. */
};

/* Variant part passed to qsort invocations. */
struct qsort {
	bool used;		/* True if slot is in use. */
	struct common *common;	/* Common shared elements. */
	void *a;		/* Array base. */
	size_t n;		/* Number of elements. */
	pthread_t id;		/* Thread id. */
	enum start_cond sc;	/* Start condition. */
	struct qsort *sibling;	/* Sibling for giving a go ahead after
				   joining joinid. */
	bool ga;		/* A sibling tests this to go ahead. */
	pthread_mutex_t mtx_ga;	/* For signalling go ahead. */
	pthread_cond_t cond_ga;	/* For signalling go ahead. */
	pthread_t joinid;	/* Thread to join before starting. */
};

/* Invariant common part, shared across invocations. */
struct common {
	int swaptype;		/* Code to use for swapping */
	struct chunk *chunk;	/* Chunk of work. */
	size_t es;		/* Element size. */
	void *thunk;		/* Thunk for qsort_r */
	cmp_t *cmp;		/* Comparison function */
	int nthreads;		/* Number of working threads. */
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
};

static void *qsort_algo(void *p);
static struct qsort *qsort_launch(struct qsort *qs);

/* The multithreaded qsort public interface */
void
qsort_mt(void *a, size_t n, size_t es, cmp_t *cmp, int maxthreads, int forkelem)
{
	int ncpu;
	struct qsort qs;
	struct common c;
	int i, islot;
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
	if ((c.slots = (struct qsort *)calloc(maxthreads, sizeof(struct qsort))) ==NULL)
		goto f6;
	for (islot = 0; islot < maxthreads; islot++) {
		if (pthread_mutex_init(&c.slots[islot].mtx_ga, NULL) != 0)
			goto f7;
		if (pthread_cond_init(&c.slots[islot].cond_ga, NULL) != 0) {
			verify(pthread_mutex_destroy(&c.slots[islot].mtx_ga));
			goto f7;
		}
		c.slots[islot].common = &c;
	}

	/* All systems go. */
	bailout = false;

	/* Initialize common elements. */
	c.swaptype = ((char *)a - (char *)0) % sizeof(long) || \
		es % sizeof(long) ? 2 : es == sizeof(long)? 0 : 1;
	c.es = es;
	c.cmp = cmp;
	c.forkelem = forkelem;
	c.nthreads = maxthreads;
	c.nslots = maxthreads * 3;
	c.workingslots = 0;
	c.activeslots = 0;

	/* Hand out the first work batch. */
	qs.a = a;
	qs.n = n;
	qs.common = &c;
	qs.sibling = NULL;
	qs.sc = sc_none;
	qsort_launch(&qs);

	/* Wait for all threads to finish */
	verify(pthread_mutex_lock(&c.mtx_done));
	for (;;) {
		verify(pthread_cond_wait(&c.cond_done, &c.mtx_done));
		DLOG("Got completion signal.\n");
		verify(pthread_mutex_lock(&c.mtx_common));
		if (c.activeslots == 0)
			break;
		verify(pthread_mutex_unlock(&c.mtx_common));
	}
	verify(pthread_mutex_unlock(&c.mtx_done));
	verify(pthread_mutex_unlock(&c.mtx_common));

	/* Free acquired resources. */
	DLOG("maxthreads=%d islot=%d.\n", maxthreads, islot);
f7:	for (i = 0; i < islot; i++) {
		DLOG("Destroy resource %d=%p.\n", i, c.slots[i].mtx_ga);
		verify(pthread_mutex_destroy(&c.slots[i].mtx_ga));
		verify(pthread_cond_destroy(&c.slots[i].cond_ga));
	}
	free(c.slots);
f6:	verify(pthread_cond_destroy(&c.cond_done));
f5:	verify(pthread_cond_destroy(&c.cond_slot));
f4:	verify(pthread_mutex_destroy(&c.mtx_done));
f3:	verify(pthread_mutex_destroy(&c.mtx_slot));
f2:	verify(pthread_mutex_destroy(&c.mtx_common));
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
 * Return a pointer to the allocated data structure.
 */
static struct qsort *
qsort_launch(struct qsort *qs)
{
	int i;
	struct qsort *sqs;

	DLOG("%10s n=%-10d Start at %p\n", "Launcher", qs->n, qs->a);
#ifdef DEBUG_SORT
	for (i = 0; i < qs->n; i++)
		fprintf(stderr, "%d ", ((int*)qs->a)[i]);
	putc('\n', stderr);
#endif
	for (;;) {
		verify(pthread_mutex_lock(&qs->common->mtx_common));
		if (qs->common->workingslots < qs->common->nthreads) {
			qs->common->workingslots++;
			qs->common->activeslots++;
			/* Find the empty slot */
			for (i = 0; i < qs->common->nslots; i++)
				if (!qs->common->slots[i].used) {
					sqs = &qs->common->slots[i];
					sqs->a = qs->a;
					sqs->n = qs->n;
					sqs->sc = qs->sc;
					sqs->sibling = qs->sibling;
					sqs->joinid = qs->joinid;
					sqs->used = true;
					sqs->ga = false;
					qs = sqs;
					break;
				}
			assert(i != qs->common->nslots);
			verify(pthread_mutex_unlock(&qs->common->mtx_common));
			if (pthread_create(&qs->id, NULL, qsort_algo, qs) == 0) {
				DLOG("%10x n=%-10d Started new thread: i=%d activeslots=%d\n",
				    qs->id, qs->n, i, qs->common->activeslots);
				return (qs);
			} else if (errno == EAGAIN) {
				/* Could sleep(2), but probably faster to qsort(3). */
				verify(pthread_mutex_lock(&qs->common->mtx_common));
				qs->common->workingslots--;
				qs->common->activeslots--;
				qs->used = false;
				verify(pthread_mutex_unlock(&qs->common->mtx_common));
				qsort(qs->a, qs->n, qs->common->es, qs->common->cmp);
				/* XXX should include a syslog call here */
				DLOG("EAGAIN on create_thread\n");
				fprintf(stderr, "EAGAIN on create_thread\n");
				return (NULL);
			} else {
				/* XXX Remove printf/perror from production release */
				fprintf(stderr, "active=%d, working=%d\n",
				    qs->common->workingslots,
				    qs->common->activeslots);
				perror("pthread_create");
				assert(("pthread_create failed", false));
			}
		} else
			verify(pthread_mutex_unlock(&qs->common->mtx_common));
		/* Wait for a thread to finish. */
		DLOG("%10s n=%-10d Wait for thread termination\n", "Launcher", qs->n);
		verify(pthread_mutex_lock(&qs->common->mtx_slot));
		verify(pthread_cond_wait(&qs->common->cond_slot,
		    &qs->common->mtx_slot));
		verify(pthread_mutex_unlock(&qs->common->mtx_slot));
	}
}

/* Thread-callable quicksort. */
static void *
qsort_algo(void *p)
{
	char *pa, *pb, *pc, *pd, *pl, *pm, *pn;
	int d, r, swaptype, swap_cnt;
	int nchildren = 0;		/* Number of threads to launch. */
	struct qsort *qs, left, right, *lleft, *lright;
	void *a;			/* Array of elements. */
	size_t n, es;			/* Number of elements; size. */
	cmp_t *cmp;
	int nl, nr;
	struct common *c;
	pthread_t id;

	qs = p;
	id = qs->id;
	/* Initialize traditional qsort arguments. */
	c = qs->common;
	a = qs->a;
	n = qs->n;
	es = c->es;
	cmp = c->cmp;
	swaptype = c->swaptype;
	DLOG("%10x n=%-10d Running thread sc=%d.\n", id, n, qs->sc);
	/* Wait for a go ahead, if needed. */
	switch (qs->sc) {
	case sc_join:	/* Wait for our parent to terminate. */
		DLOG("%10x n=%-10d Waiting to join %x.\n", id, n, qs->joinid);
		verify(pthread_join(qs->joinid, NULL));
		if (qs->sibling) {
			/* Give our sibling a go ahead. */
			DLOG("%10x n=%-10d Signalling %x.\n", id, n, qs->sibling->id);
			verify(pthread_mutex_lock(&qs->sibling->mtx_ga));
			qs->sibling->ga = true;
			verify(pthread_cond_signal(&qs->sibling->cond_ga));
			verify(pthread_mutex_unlock(&qs->sibling->mtx_ga));
		}
		break;
	case sc_signal:	/* Wait for our sibling to give us a go ahead. */
		DLOG("%10x n=%-10d Waiting for signal.\n", id, n);
		verify(pthread_mutex_lock(&qs->mtx_ga));
		while (!qs->ga)
			verify(pthread_cond_wait(&qs->cond_ga, &qs->mtx_ga));
		verify(pthread_mutex_unlock(&qs->mtx_ga));
		break;
	case sc_none:
		break;
	}
	DLOG("%10x n=%-10d Got the go ahead\n", id, n);
#ifdef DEBUG_SORT
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
	else
		nchildren++;
	right.n = (pd - pc) / es;
	if (right.n > 0 && right.n <= c->forkelem)
		qsort(a, n, es, cmp);
	else
		nchildren++;
	/*
	 * At this point all the hard work is done.  We mark
	 * a slot as available, so that the launcher won't deadlock.
	 */
done:
	DLOG("%10x n=%-10d Hard work finished ln=%d rn=%d.\n", id, n, left.n, right.n);
	verify(pthread_mutex_lock(&c->mtx_common));
	c->workingslots--;
	/* Free our slot, and stop using it. */
	qs->used = false;
	qs = NULL;
	/* Indicate that a slot is now free. */
	DLOG("%10x n=%-10d Signal free slot.\n", id, n);
	verify(pthread_mutex_lock(&c->mtx_slot));
	verify(pthread_cond_signal(&c->cond_slot));
	verify(pthread_mutex_unlock(&c->mtx_slot));
	verify(pthread_mutex_unlock(&c->mtx_common));
	/* Launch threads for the two halfs. */
	lleft = NULL;
	if (left.n > c->forkelem) {
		left.common = c;
		left.a = a;
		left.sc = sc_join;
		left.joinid = id;
		lleft = qsort_launch(&left);
		if (lleft == NULL)
			nchildren--;
	}
	if (right.n > c->forkelem) {
		right.common = c;
		right.a = pn - right.n * es;
		if (nchildren == 2)
			right.sc = sc_signal;
		else {
			right.sc = sc_join;
			right.joinid = id;
		}
		lright = qsort_launch(&right);
		if (lright == NULL)
			nchildren--;
	}
	if (nchildren == 2)
		lleft->sibling = lright;
	else if (lleft)
		lleft->sibling = NULL;
	/* Detach if nobody will ask to join us. */
	if (nchildren == 0)
		pthread_detach(id);
	DLOG("%10x n=%-10d Sub-launchers finished.\n", id, n);
	/*
	 * At this point any additional threads have been launched.
	 * If all slots are free, it means that all our work is done.
	 */
	verify(pthread_mutex_lock(&c->mtx_common));
	c->activeslots--;
	if (c->activeslots == 0) {
		DLOG("%10x n=%-10d Signalling completion.\n", id, n);
		verify(pthread_cond_signal(&c->cond_done));
	}
	verify(pthread_mutex_unlock(&c->mtx_common));
	DLOG("%10x n=%-10d Finished.\n", id, n);
}

#ifdef TEST
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
#ifdef DEBUG_SORT
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
