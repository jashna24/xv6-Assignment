#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "defs.h"
#include "spinlock.h"


int pr =60;
struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;


#ifdef MLFQ 
void que_push(struct proc *p)
{
  int que = p->qu;
  if((queues[que].back - queues[que].front + NPROC)%NPROC  == NPROC -1)
  {
    cprintf("The queue is full\n");
  }
  else
  {
    if(queues[que].front == -1)
      queues[que].front = 0;
    queues[que].back = (queues[que].back + 1)%NPROC;
    queues[que].arr[queues[que].back] = p;
  }
}


void que_pop(struct proc *p)
{
  int que = p->qu;
  if(queues[que].front == -1)
  {
    cprintf("The queue is empty\n");
  }
  else
  {
    if(queues[que].front == queues[que].back)
    {
      queues[que].front = -1;
      queues[que].back = -1;
    }
    else
      queues[que].front = (queues[que].front + 1)%NPROC;
  }
}

#endif

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  #ifdef PBS
    p->priority = 60;
  #endif

  p->stime = ticks;
  p->etime  = 0;
  p->rtime  = 0;
  p->iotime = 0;

  #ifdef MLFQ
    p->prtime = 0;
    p->pwtime = 0;
    p->qu = 0;
    p->pinfo.pid = p->pid;
    p->pinfo.runtime =0;
    p->pinfo.num_run = 0;
    p->pinfo.current_queue = 0;
    for(int i=0;i<5;i++)
      p->pinfo.ticks[i] = 0;
  #endif  

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  #ifdef MLFQ
    que_push(p);
  #endif
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;
  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;
  //tester 
  cprintf("child process with pid %d\n",pid);

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  // tester PBS
  // #ifdef PBS
  //   np->priority = np->pid/2;
  // #endif

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;

  curproc->etime = ticks;
  sched();
  panic("zombie exit");
}

#ifdef MLFQ
int getpinfo(struct proc_stat *pr,int id)
{
    struct proc *p;
    // cprintf("********%d*******\n",id);
  acquire(&ptable.lock);
    pr->pid = -1;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
        if(p->pid == id)
        {
          pr->pid = p->pinfo.pid; 
          pr->num_run = p->pinfo.num_run;
          pr->runtime = p->pinfo.runtime;
          pr->current_queue = p->pinfo.current_queue;
          // cprintf("\npid %d , qu %d\n",pr->pid,pr->current_queue);
          for(int i=0;i<5;i++)
          {
            pr->ticks[i] = p->pinfo.ticks[i];
          }

          break;
        }
    }
  release(&ptable.lock);

  return 0;

}
#endif
int waitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.

        *wtime = p->etime - p->stime - p->rtime;
        *rtime = p->rtime;
        
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;)
  {
    // Enable interrupts on this processor.
    sti();


    // Loop over process table looking for process to run.
    #ifdef DEFAULT
      acquire(&ptable.lock);
      struct proc *p;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
      {
  	        if(p->state != RUNNABLE)
  	          continue;
  	        c->proc = p;
            cprintf("cpu %d Pname %s pid %d runtime %d\n",c->apicid,p->name,p->pid,p->rtime);
  	        switchuvm(p);
  	        p->state = RUNNING;
  	        swtch(&(c->scheduler), p->context);
  	        switchkvm();
  	        c->proc = 0;
      }
      release(&ptable.lock);
    #else
      #ifdef FCFS
        acquire(&ptable.lock);
        struct proc *p;
        struct proc *min_ctime = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
          if(p->state != RUNNABLE)
          continue;

          if(min_ctime != 0)
          {
            if(p->stime < min_ctime->stime)
              min_ctime = p;
          }
          else
          {
            min_ctime = p;
          }
        }

        if(min_ctime !=0)
        { 
            cprintf("cpu %d Pname %s pid %d runtime %d\n",c->apicid,min_ctime->name,min_ctime->pid,min_ctime->rtime);
          c->proc = min_ctime;
          switchuvm(min_ctime);
          min_ctime->state = RUNNING;

          swtch(&(c->scheduler), min_ctime->context);
          switchkvm();
          c->proc = 0;
        } 
        release(&ptable.lock);
    #else
      #ifdef PBS
        acquire(&ptable.lock);
        struct proc *maxP = 0;
        struct proc *p;
        struct proc *q = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
        {
          if(p->state != RUNNABLE)
          continue;
      	  maxP = p;
          for(q = ptable.proc; q < &ptable.proc[NPROC]; q++)
          {
          	if(q->state != RUNNABLE)
          		continue;
          	if(maxP->priority > q->priority)
            maxP = q;
          }

	        if(maxP !=0)
	        {  
            cprintf("cpu %d Pname %s pid %d priority %d runtime %d\n",c->apicid,maxP->name,maxP->pid,maxP->priority,maxP->rtime);
	          c->proc = maxP;
	          switchuvm(maxP);
	          maxP->state = RUNNING;

	          swtch(&(c->scheduler), maxP->context);
	          switchkvm();
	          c->proc = 0;
	        }  
        }
        
        release(&ptable.lock);
    #else
      #ifdef MLFQ
        acquire(&ptable.lock);
        struct proc *p=0;
        for(int i =0;i<5;i++)
        {
          if(queues[i].front != -1 && queues[i].arr[queues[i].front]->state == RUNNABLE)
          {
            p = queues[i].arr[queues[i].front];
            que_pop(p);
            break;
          }

        }
        if(p!=0)
        {   
            p->pinfo.num_run++;
            cprintf("queue: %d, pid: %d,rtime: %d, wtime: %d\n",p->qu,p->pid,p->rtime,p->pwtime);
            c->proc = p;
            switchuvm(p);
            p->state = RUNNING;

            swtch(&(c->scheduler), p->context);
            switchkvm();
            c->proc = 0;
        }
        release(&ptable.lock);
      #endif    
      #endif    
      #endif    
      #endif    


  }
}
  
// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  #ifdef PBS 
    acquire(&ptable.lock);  //DOC: yieldlock
    struct proc *p;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    {
      if(p->state != RUNNABLE)
      continue;

      if(p->priority <= myproc()->priority)
      { 
      	// cprintf("Pid: %d got preemptied\n",myproc()->pid);
        myproc()->state = RUNNABLE;
        sched();
      }  

    }
    release(&ptable.lock);
  #else
    #ifdef DEFAULT
      acquire(&ptable.lock);  //DOC: yieldlock
      myproc()->state = RUNNABLE;
      sched();
      release(&ptable.lock);
  #else
    #ifdef MLFQ
      acquire(&ptable.lock);  //DOC: yieldlock
      if(myproc()->prtime >= queues[myproc()->qu].qrtime)
      {
        if(myproc()->qu < 4)
          myproc()->qu++;
        cprintf("demotion of pid: %d to %d\n",myproc()->pid,myproc()->qu);

        myproc()->pinfo.current_queue = myproc()->qu;
        // cprintf("\npid: %d queue: %d\n",myproc()->pid,myproc()->pinfo.current_queue);
        myproc()->prtime = 0;
        myproc()->pwtime = 0;
        myproc()->state = RUNNABLE;
        que_push(myproc());
        sched();
      }
      else
      {
        int prempt = 0;
        for(int j =0;j<myproc()->qu;j++)
        {
          if(queues[j].front != -1 && queues[j].arr[queues[j].front]->state == RUNNABLE)
          {
            prempt = 1;
            break;
          }
        }

        if(prempt == 1)
        {
          myproc()->prtime = 0;
          myproc()->pwtime = 0;
          myproc()->state = RUNNABLE;
          que_push(myproc());
          sched();   
        }
      }  
      release(&ptable.lock);
  #endif
  #endif
  #endif

}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
    {
      p->state = RUNNABLE;
      #ifdef MLFQ
        p->pwtime = 0;
        p->prtime = 0;
        que_push(p);
      #endif
    }    
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  // #ifdef MLFQ
  //   for(int i = 0; i < 5; i++)
  //   {
  //     if(queues[i].front == -1)
  //     {
  //       cprintf("Queue %d is empty\n", i);
  //       continue;
  //     }
  //     int flag = 1;
  //     for(int j = queues[i].front; flag || j%NPROC != (queues[i].back+1)%NPROC; j++)
  //     {
  //       flag = 0;
  //       cprintf("queue %d  pid %d\n", i, queues[i].arr[j%NPROC]->pid);
  //     } 
  //   }
  // #endif

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}



void update_time()
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {

    if(p)
      {
        if(p->state == SLEEPING)
          p->iotime++;
        else if(p->state == RUNNING)
        {
          p->rtime++;
        }
      }  
  }
  release(&ptable.lock);

}

int set_priority(int pid, int priority)
{
  struct proc *p;
  int old_priority= -1;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->pid == pid) 
    {
      old_priority = p->priority;
      p->priority = priority;
      break;
    }
  }
  release(&ptable.lock);

  return old_priority;
}

#ifdef MLFQ
void aging()
{
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
  {
    if(p->state == RUNNABLE)
    {
      p->pwtime++;
    }  
    else if(p->state == RUNNING)
    {  
      p->prtime++;
      p->pinfo.runtime++;
      p->pinfo.ticks[p->qu]++;
    }
  }

  for(int i =0;i<5;i++)
  {
    while(queues[i].front != -1 && queues[i].arr[queues[i].front]->state == RUNNABLE && queues[i].arr[queues[i].front]->pwtime >= queues[i].qwtime)
    {
      p = queues[i].arr[queues[i].front];
      if(p->qu > 0)
        {
          que_pop(p);
          p->qu--;
          cprintf("aging of pid: %d to %d\n",p->pid,p->qu);
          p->pinfo.current_queue = p->qu;
          // cprintf("\npid: %d queue: %d\n",p->pid,p->pinfo.current_queue);

          p->pwtime = 0;
          p->prtime = 0;
          que_push(p);
        }
    }
  }
  release(&ptable.lock);
}

#endif