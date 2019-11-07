#include "types.h"
#include "stat.h"
#include "user.h"
int main()
{
    int w,r,ret,p[4];
    if(fork()==0)
    {
        for(int i=0;i<4;i++)
        {
             p[i]=fork();
            if(p[i]==0)
            {
                printf(1,"proc %d.... \n",p[i]);
                for(int i=0;i<=10000000;i++)
                    printf(1,"");
                // exec(argv[1],argv);
                exit();
            }
        }
        for(int i=0;i<4;i++){
            printf(1,"proc %d... pid %d\n",i,p[i]);
            set_priority(p[i],60+4-i);
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