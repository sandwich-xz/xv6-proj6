#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
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
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_mmap(void)
{
  uint64 addr;
  int len;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argint(1, &len);
  if (len <= 0)
    return -1;
  if (addr != 0)
    return -1; // 暂不支持指定地址
  len = (len + PGSIZE - 1) & ~(PGSIZE - 1);

  // 直接按 sbrk 的方式长在进程尾部：退出时 uvmfree 会统一回收，
  // 不用额外维护一段独立的映射区。
  uint64 old = p->sz;
  if (uvmalloc(p->pagetable, old, old + len, PTE_W | PTE_X) == 0)
    return -1;
  p->sz = old + len;

  // kalloc 给的是脏页，必须清零。
  // 注意：这版 xv6 的内核直接映射是恒等的（虚址 == 物理址），不能加 KERNBASE。
  for (uint64 a = old; a < old + len; a += PGSIZE) {
    uint64 pa = walkaddr(p->pagetable, a);
    if (pa == 0)
      return -1;
#ifdef ARCH_LOONGARCH
    memset((void *)(pa | DMWIN_MASK), 0, PGSIZE);
#else
    memset((void *)pa, 0, PGSIZE);
#endif
  }
  return old;
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if(addr + n < addr)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
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

  argint(0, &pid);
  return kkill(pid);
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
