#include "types.h"
#include "fs.h"
#include "user.h"
#include "stat.h"
#include "pinfo.h"

int main(int argc, char *argv[])
{	
	#ifdef MLFQ
	int pid;
	struct proc_stat p;

	pid = atoi(argv[1]);
	getpinfo(&p, pid);
	if(p.pid !=-1)
		printf(1,"pid: %d\nruntime:%d\nnum_run: %d\ncurrent_queue: %d\nticks: %d %d %d %d %d\n",p.pid,p.runtime,p.num_run,p.current_queue,p.ticks[0],p.ticks[1],p.ticks[2],p.ticks[3],p.ticks[4]);
	else
	{
		printf(1,"pid not found\n");
	}
	#endif

	exit();
}