// Linux (riscv64) 系统调用兼容子集。
// 目的：让按 Linux ABI 编译的程序（busybox 与国赛测例）能在本内核上运行。
// 分发约定：a7 >= 56 视为 Linux 号，由 syscall() 转到这里。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"

#define L_write 64
#define L_writev 66
#define L_read 63
#define L_openat 56
#define L_close 57
#define L_lseek 62
#define L_exit 93
#define L_exit_group 94
#define L_brk 214
#define L_munmap 215
#define L_mmap 222
#define L_mprotect 226
#define L_set_tid_address 96
#define L_set_robust_list 99
#define L_rseq 293
#define L_rt_sigaction 134
#define L_rt_sigprocmask 135
#define L_prctl 167
#define L_uname 160
#define L_getpid 172
#define L_getuid 174
#define L_geteuid 175
#define L_getgid 176
#define L_getegid 177
#define L_clock_gettime 113
#define L_gettimeofday 169
#define L_getrandom 278
#define L_ioctl 29

struct linux_iovec {
  uint64 base;
  uint64 len;
};

static struct file *
lfd(uint64 fd)
{
  struct proc *p = myproc();
  if (fd >= NOFILE)
    return 0;
  return p->ofile[fd];
}

// 只读打开（够 hello/world 与 busybox 启动用；写/创建后续补）
static uint64
l_openat(char *path, int flags)
{
  struct file *f;
  struct inode *ip;
  int fd;

  (void)flags;
  if ((ip = namei(path)) == 0)
    return -1;
  ilock(ip);
  if ((f = filealloc()) == 0) {
    iunlockput(ip);
    return -1;
  }
  f->type = FD_INODE;
  f->ip = ip;
  f->off = 0;
  f->readable = 1;
  f->writable = 0;
  iunlock(ip);
  if ((fd = fdalloc(f)) < 0) {
    fileclose(f);
    return -1;
  }
  return fd;
}

// 匿名 mmap：按需求增长 p->sz 并把新页清零，忽略 addr 提示。
static uint64
l_mmap(uint64 len)
{
  struct proc *p = myproc();
  uint64 old = p->sz;
  uint64 need = PGROUNDUP(len);

  if (need == 0)
    need = PGSIZE;
  if (growproc((int)need) < 0)
    return (uint64)-1;
  for (uint64 a = old; a < p->sz; a += PGSIZE) {
    uint64 pa = walkaddr(p->pagetable, a);
    if (pa)
      memset((void *)pa, 0, PGSIZE);
  }
  return old;
}

uint64
sys_linux(uint64 num)
{
  struct proc *p = myproc();
  uint64 a0 = p->trapframe->a0;
  uint64 a1 = p->trapframe->a1;
  uint64 a2 = p->trapframe->a2;

  switch (num) {
  case L_write: {
    struct file *f = lfd(a0);
    if (f == 0)
      return -1;
    return filewrite(f, a1, (int)a2);
  }
  case L_writev: {
    struct file *f = lfd(a0);
    struct linux_iovec iov[8];
    uint64 total = 0;

    if (f == 0)
      return -1;
    if (a2 > 8)
      a2 = 8;
    if (copyin(p->pagetable, (char *)iov, a1, a2 * sizeof(struct linux_iovec)) < 0)
      return -1;
    for (uint64 i = 0; i < a2; i++) {
      int n = filewrite(f, iov[i].base, (int)iov[i].len);
      if (n < 0)
        return total ? total : (uint64)-1;
      total += n;
    }
    return total;
  }
  case L_read: {
    struct file *f = lfd(a0);
    if (f == 0)
      return -1;
    return fileread(f, a1, (int)a2);
  }
  case L_openat: {
    char path[MAXPATH];
    if (copyinstr(p->pagetable, path, a1, MAXPATH) < 0)
      return -1;
    return l_openat(path, (int)a2);
  }
  case L_close: {
    struct file *f = lfd(a0);
    if (f == 0)
      return -1;
    fileclose(f);
    p->ofile[a0] = 0;
    return 0;
  }
  case L_lseek: {
    struct file *f = lfd(a0);
    if (f == 0)
      return -1;
    if ((int)a2 == 0)
      f->off = (uint)a1;
    else if ((int)a2 == 1)
      f->off += (uint)a1;
    return f->off;
  }
  case L_exit:
  case L_exit_group:
    kexit((int)a0);
    return 0;
  case L_brk: {
    uint64 old = p->sz;
    if (a0 == 0)
      return old;
    if (a0 < old)
      return old;
    if (growproc((int)(a0 - old)) < 0)
      return old;
    return p->sz;
  }
  case L_mmap:
    return l_mmap(a1);
  case L_munmap:
  case L_mprotect:
    return 0;
  case L_set_tid_address:
  case L_set_robust_list:
    return p->pid;
  case L_rseq:
  case L_rt_sigaction:
  case L_rt_sigprocmask:
  case L_prctl:
  case L_ioctl:
    return 0;
  case L_uname: {
    char u[64 * 6];
    memset(u, 0, sizeof(u));
    strncpy(u, "Linux", 5);
    if (copyout(p->pagetable, a0, u, sizeof(u)) < 0)
      return -1;
    return 0;
  }
  case L_getpid:
    return p->pid;
  case L_getuid:
  case L_geteuid:
  case L_getgid:
  case L_getegid:
    return 0;
  case L_clock_gettime:
  case L_gettimeofday: {
    uint64 tv[2];
    uint64 t;
    acquire(&tickslock);
    t = ticks;
    release(&tickslock);
    tv[0] = t / 100;
    tv[1] = (t % 100) * 10000000ULL;
    if (copyout(p->pagetable, a0, (char *)tv, sizeof(tv)) < 0)
      return -1;
    return 0;
  }
  case L_getrandom: {
    char r[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    int n = a2 > 8 ? 8 : (int)a2;
    if (copyout(p->pagetable, a0, r, n) < 0)
      return -1;
    return n;
  }
  default:
    printf("linux: unimplemented syscall %ld (pid %d)\n", num, p->pid);
    return -1;
  }
}
