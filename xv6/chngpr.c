#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[])
{
	int pid,val;

	if (argc < 3)
	{
		printf(2,"please enter the command in right format\n");
		exit();
	}


	pid = atoi(argv[1]);
	val = atoi(argv[2]);

	#ifdef MLF
		;
	#else
		if(val > 100 || val < 0)
		{
			printf(2,"priority value not in the range\n");
			exit();
		}
	#endif

	int old_pid = set_priority(pid,val);
	if(old_pid == -1)
		printf(2,"Pid does not exists\n");
	else
		printf(2,"old priority: %d\n",old_pid);		 
	exit();

}