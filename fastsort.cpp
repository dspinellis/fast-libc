#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <sys/stat.h>

static void
error(const char *s)
{
	perror(s);
	exit(1);
}

main(int argc, char *argv[])
{
	int fd, i, iocnt, ioalloc;
	struct stat sb;
	char *p, *end, *linestart;
	struct iovec *iov;
	bool newline;

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
	iov = malloc(ioalloc * sizeof(struct iovec));
	if (iov == NULL)
		error("malloc");
	linestart = p;
	end = p + sb.st_size;
	for (; p < end; p++)
		if (*p == '\n') {
			iov[iocnt].iov_base = linestart;
			iov[iocnt].iov_len = p - linestart + 1;
			linestart = p + 1;
			iocnt++;
			if (iocnt > ioalloc) {
				ioalloc *= 2;
				iov = realloc(iov, ioalloc * sizeof(*iov));
				if (iov == NULL)
					error("realloc");
			}
		}
	if (p[-1] != '\n') {
		fprintf(stderr, "File does not end with a newline\n");
		exit(1);
	}
	writev(1, iov, iocnt);
	return (0);
}
