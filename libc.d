#!/usr/sbin/dtrace -s

#pragma D option destructive

/*
 * The process we were launched to watch has been stopped
 * waiting for us.  Resume it.
 */
BEGIN {
	system("prun %d", $1);
	self->entry = self->systime = self->libctime = 0;
}

/*
 * The process we were launched to watch is terminating.
 * We should also exit, because our work is done.
 */
proc:::exit
/pid == $1/
{
	printf("\nCommand: %s\n", execname);
	printf("\nTimes: %d wall, %d system, %d libc\n",
		vtimestamp - self->entry,
		self->systime,
		self->libctime);
	exit(0);
}

/*
 * Determine appropriate depth to avoid tracking nested invocations.
 */
pid$1:a.out:main:entry
{
	self->inlibc = 1;
	self->entry = vtimestamp;
}

/*
 * Entering a libc function. Avoid nested invocations and
 * ti_bind_* calls.  Clear timing data and setup exit
 * monitoring.
 */
pid$1:libc::entry
/self->inlibc && !self->infunc && 
    probefunc != "_ti_bind_guard" && 
    probefunc != "_ti_bind_clear"/
{
	@calls[probefunc] = count();
	self->lcstart = vtimestamp;
	self->lsystime = 0;

	/* Don't fire again until we exit. */
	self->infunc = 1;
	self->name = probefunc;
}

/* Returning from a called libc function. */
pid$1:libc::return
/probefunc == self->name/
{
	self->infunc = 0;
	self->libctime += vtimestamp - self->lcstart;
	@call_rtime[probefunc] = sum(vtimestamp - self->lcstart);
	@call_stime[probefunc] = sum(self->lsystime);
}

/* Entering a system call inside libc. */
syscall:::entry
/pid == $1/
{
	self->sysstart = vtimestamp;
}

/* Return from a system call inside libc. */
syscall:::return
/pid == $1 && self->infunc/
{
	self->lsystime += vtimestamp - self->sysstart;
	self->systime += vtimestamp - self->sysstart;
}

/* Return from a system call outside libc. */
syscall:::return
/pid == $1 && !self->infunc/
{
	self->systime += vtimestamp - self->sysstart;
}

END
{
	printf("\nCalls\n");
	printa(@calls);
	printf("\nTotal clock time\n");
	printa(@call_rtime);
	printf("\nTotal system time\n");
	printa(@call_stime);
}
