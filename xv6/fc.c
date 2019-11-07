#include "types.h"
#include "stat.h"
#include "user.h"
int main()
{
    int w,r,ret;
    if(fork()==0)
    {
        for(int i=0;i<4;i++)
        {
            int p=fork();
            if(p==0)
            {
                printf(1,"proc %d.... \n",i);
                for(int i=0;i<=10000000;i++)
                    printf(1,"");
                // exec(argv[1],argv);
                exit();
            }
        }
        for(int i=0; i<4;i++)
        {
            ret=waitx(&w,&r);
            printf(1,"pid-%d rtime-%d wtime-%d\n",ret,r,w);
        }
        exit();
    }
    ret=waitx(&w,&r);
    printf(1,"parent proc:- pid-%d rtime-%d wtime-%d\n",ret,w,r);
    exit();
}