#!/usr/sbin/dtrace -s

#pragma D option destructive

int fileid;

proc:::exec-success
/pid == $target || progenyof($target)/
{
	self->traceme = 1;
	@proc[execname] = count();
}

/* This should fire AFTER libc has been dloaded. */
syscall::setcontext:entry
/self->traceme/
{
	self->traceme = 0;
	stop();
	system("dtrace -q -s /home/dds/src/dtrace/libc.d -o /home/dds/src/dtrace/data/%07d-%d.out %d &", ++fileid, pid, pid);
}

dtrace:::END
{
	printa(@proc);
}
