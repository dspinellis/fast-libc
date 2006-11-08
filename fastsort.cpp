#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define min(a, b) ((a) < (b) ? (a) : (b))

static void
error(const char *s)
{
	perror(s);
	exit(1);
}

int
compare(const struct iovec *a, const struct iovec *b)
{
	int r;
	
	r = memcmp(a->iov_base, b->iov_base,
	    min(a->iov_len, b->iov_len) - 1);
	if (r == 0)
		return (a->iov_len - b->iov_len);
	else
		return (r);
}

main(int argc, char *argv[])
{
	int fd, i, iocnt, ioalloc;
	struct stat sb;
	char *p, *end, *linestart;
	struct iovec *iov;
	int iovmax;

	if ((iovmax = (int)sysconf(_SC_IOV_MAX)) == -1)
		error("sysconf");
	if ((fd = open(argv[1], 0)) < 0)
		error(argv[1]);
	if (fstat(fd, &sb) < 0)
		error("fstat");
	if (sb.st_size == 0)
		return (0);
	if ((p = mmap(0, sb.st_size, PROT_READ, MAP_NOCORE, fd, 0)) == MAP_FAILED)
		error("mmap");
	/* Index the file's lines */
	iocnt = 0;
	ioalloc = 1024;
	iov = malloc(ioalloc * sizeof(*iov));
	if (iov == NULL)
		error("malloc");
	linestart = p;
	end = p + sb.st_size;
	for (;;) {
		p = memchr(linestart, '\n', end - linestart);
		if (p == NULL) {
			if (linestart != end) {
				fprintf(stderr, "File does not end with a newline\n");
				exit(1);
			} else
				break;
		}
		if (iocnt >= ioalloc) {
			ioalloc *= 2;
			iov = realloc(iov, ioalloc * sizeof(*iov));
			if (iov == NULL)
				error("realloc");
		}
		iov[iocnt].iov_base = linestart;
		iov[iocnt].iov_len = p - linestart + 1;
		linestart = p + 1;
		iocnt++;
	}
	qsort(iov, iocnt, sizeof(*iov), compare);
	/* Write result in iovmax chunks */
	while (iocnt > 0) {
		int count, n;

		count = min(iovmax, iocnt);
		if (writev(1, iov, count) == -1)
			error("writev");
		iov += count;
		iocnt -= count;
	}
	return (0);
}
