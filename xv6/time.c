#include "types.h"
#include "fs.h"
#include "user.h"
#include "stat.h"

int main(int argc, char *argv[])
{
	int pid,run_time,wait_time;
	int status;

	pid = fork();

	char *com[50];
	for(int i=0;i<50;i++)
	{
		com[i] = (char*)malloc(sizeof(char)*100);
	}
	int len  = argc;
	for(int i = 1;i<len;i++)
	{
		com[i-1]=argv[i];
	}
	com[argc-1] = 0;


	if(pid==0)
	{
		exec(com[0], com);
    	printf(1, "exec %s failed\n", argv[1]);
	}

	else
	{	
		printf(1,"%d\n",pid);
		status = waitx(&wait_time, &run_time);
		printf(1,"run time: %d, wait time: %d and status: %d\n",run_time,wait_time,status);
	}

	exit();
}