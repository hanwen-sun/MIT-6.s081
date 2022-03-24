#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"
// #include "proc.c"
// #include "kalloc.c"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)    // 用户调用该函数， 增加或减少自己的用户地址空间;
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

uint64
sys_trace(void)
{
  int mask;

  if(argint(0, &mask) < 0)
    return -1;
  myproc()->mask = mask;

  return 0;
}

uint64
sys_sysinfo(void) 
{
  struct sysinfo s;
  uint64 st; // user pointer to struct stat
  
  s.freemem = kfreemem();   // 得到空闲内存的字节数;   注意，这几个函数一定要在defs.h中申明;
  s.nproc = getproc();  // 得到进程数;

  if(argaddr(0, &st) < 0)   // st应该是user_space的调用地址空间?   这里是得到 系统调用 a0 的值并存在指针St中, 其中a0是函数系统调用的返回值;
    return -1;                 // 指针形式就是返回地址?
                            // st是物理内存页?这里真不清楚，不知道第一个系统参数是什么;
  struct proc *p = myproc();
  if(copyout(p->pagetable, st, (char *)&s, sizeof(s)) < 0)       // 把struct sysinfo s (把sysinfo结构体拷贝回内存空间)
      return -1;
  return 0;
}