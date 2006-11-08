#include <vector>
#include <algorithm>
#include <functional>

#include <string>
#include <iostream>

#include <cstdlib>
#include <cstdio>
#include <cstring>

using namespace std;

#include <fcntl.h>
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

	vector <struct iovec> iov;

struct compare : public binary_function <const struct iovec &, const struct iovec &, bool> {
	bool operator()(const struct iovec &a, const struct iovec &b) const {
		int r;
		
		r = memcmp(a.iov_base, b.iov_base,
		    min(a.iov_len, b.iov_len) - 1);
		if (r == 0)
			r = (a.iov_len - b.iov_len);
		return (r < 0);
	}
};

int
main(int argc, char *argv[])
{
	int fd, i, count, iocnt;
	struct stat sb;
	char *p, *end, *linestart;
	int iovmax;
	struct iovec v;

	if ((iovmax = (int)sysconf(_SC_IOV_MAX)) == -1)
		error("sysconf");
	if ((fd = open(argv[1], 0)) < 0)
		error(argv[1]);
	if (fstat(fd, &sb) < 0)
		error("fstat");
	if (sb.st_size == 0)
		return (0);
	if ((p = (char *)mmap(0, sb.st_size, PROT_READ, MAP_NOCORE, fd, 0)) == MAP_FAILED)
		error("mmap");
	/* Index the file's lines */
	linestart = p;
	end = p + sb.st_size;
	for (;;) {
		p = (char *)memchr(linestart, '\n', end - linestart);
		if (p == NULL) {
			if (linestart != end) {
				fprintf(stderr, "File does not end with a newline\n");
				exit(1);
			} else
				break;
		}
		v.iov_base = linestart;
		v.iov_len = p - linestart + 1;
		iov.push_back(v);
		linestart = p + 1;
	}
	sort(iov.begin(), iov.end(), compare());
	/* Write result in iovmax chunks */
	for (iocnt = iov.size(), i = 0; iocnt > 0; iocnt -= count, i += count) {

		count = min(iovmax, iocnt);
		if (writev(1, &iov[i], count) == -1)
			error("writev");
		iocnt -= count;
	}
	return (0);
}
