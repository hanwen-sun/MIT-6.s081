#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "fs.h"
#include "fcntl.h"
#include "file.h"


struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();
int mmap_handler(uint64 va, int cause);
extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
//
void
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  
  // save user program counter.
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // system call

    if(p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    intr_on();

    syscall();
  } else if(r_scause() == 13 || r_scause() == 15) {
    // 1. 判断出错地址是否在vma范围内;  (所有都要判断吗?)
    // 2. 如果不在和pagefault一样分配;
    // 3. 否则, 先分配一个page, 再读文件
    uint64 va = r_stval();
    struct proc *p = myproc();

    // 判断va是否位于有效区间;
    if(PGROUNDUP(p->trapframe->sp) - 1 >= va || va >= p->sz) {
        printf("the va: %p is invalid address!  p->sz: %p\n", va, p->sz);
        p->killed = 1;
    } 
      
    
    if(mmap_handler(va, r_scause()) == -1) {
        printf("handle mmap error!\n");
        p->killed = 1;
    }
    //printf("page fault: %p\n", va);
    // int flag = 0;
    
    // printf("page fault done!\n");
  }else if((which_dev = devintr()) != 0){
    // ok
  } else {
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }

  if(p->killed)
    exit(-1);

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2)
    yield();

  usertrapret();
}

//
// return to user space
//
void
usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which 
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if((scause & 0x8000000000000000L) &&
     (scause & 0xff) == 9){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000001L){
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if(cpuid() == 0){
      clockintr();
    }
    
    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  } else {
    return 0;
  }
}

int mmap_handler(uint64 va, int cause) {
    int vma_id = -1;
    struct proc *p = myproc();

    for(int i = 0; i < p->vma_cnt; i++) {
      uint64 start = p->vma_[i].addr;
      uint64 end = p->vma_[i].addr + p->vma_[i].length;
      /*if(p->vma_[i].used) {
          printf("i: %d, start: %p, end: %p\n", i, start, end);
      } */

      if(va >= start && va < end) {   // 特别注意, 这里是小于;
        //printf("allocate vma page!\n");
        vma_id = i;
        break;
      }
    }
    va = PGROUNDDOWN(va);

    if(vma_id != -1) {
        //printf("allocate vma page page_fault reason: %d\n", r_scause());
        char* pa = kalloc();
        struct file *f = p->vma_[vma_id].f;
        struct vma vma_current = p->vma_[vma_id];
        if(pa == 0)
          p->killed = 1;
        else {
          if(f->readable == 0 && cause == 13)  return -1;
          if(f->writable == 0 && cause == 15)  return -1;

          int flags = 0;
          int prot = vma_current.prot;
          if(prot & PROT_READ)
            flags |= PTE_R;
          if(prot & PROT_WRITE)
            flags |= PTE_W;
          if(prot & PROT_EXEC)
            flags |= PTE_X;

          flags |= PTE_U;   // 特别注意, 这里要赋值, 才能给user调用;
          flags |= PTE_V;
          memset(pa, 0, PGSIZE);   // 这里memset的作用是什么?
          //printf("vma pagefault va: %p pa: %p\n", va, pa);
          if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, flags) != 0) {
            printf("allocate page fail!\n");
            kfree(pa);
            return -1;
          }

          int new_offset = va - vma_current.addr + vma_current.offset;
          // printf("new_offset: %d\n", new_offset);
          // 读取文件具体内容;
          // 这里一定不能用fileread, 因为会改变文件实际的offset
          // 一定要利用vma的offset
          
          int r = 0;
          ilock(f->ip);
          if((r = readi(f->ip, 1, va, new_offset, PGSIZE)) <= 0) {
            printf("readi error!\n");
            return -1;
          }
            
          iunlock(f->ip);
          // p->vma_[vma_id].offset += PGSIZE;
        }
    }else {
        char* pa = kalloc();
        if(pa == 0)
          p->killed = 1;
        else {
          //printf("normal pagefault va: %p pa: %p\n", va, pa);
          memset(pa, 0, PGSIZE);
          if(mappages(p->pagetable, va, PGSIZE, (uint64)pa, PTE_R | PTE_W | PTE_X | PTE_U) != 0){   // return 0 -- success;  特别注意,
            kfree(pa);        //char* 不用转 void* ?                                             
            return -1;
          }
        }
    } 

  return 0;
}