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
#include "stat.h"
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
#define L_readlinkat 78
#define L_newfstatat 79
#define L_fstat 80
#define L_prlimit64 261
#define L_dup 23
#define L_dup3 24
#define L_fcntl 25
#define L_getppid 173
#define L_clone 220
#define L_execve 221
#define L_chdir 49
#define L_getcwd 17
#define L_setpgid 154
#define L_getpgid 155

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

static char argbuf[MAXARG][MAXPATH];

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
  case L_getppid:
    return 1;
  case L_dup:
  case L_dup3: {
    struct file *f = lfd(a0);
    int fd;
    if (f == 0)
      return -1;
    if ((fd = fdalloc(f)) < 0)
      return -1;
    return fd;
  }
  case L_fcntl:
    return 0;
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
  case L_readlinkat:
    return -1; // 没有符号链接：让调用方走 fallback
  case L_newfstatat:
  case L_fstat: {
    // Linux struct stat（riscv64）：st_mode 在 16，st_size 在 48
    char st[128];
    struct file *f;
    struct inode *ip = 0;
    uint64 size = 0;
    uint32 mode = 0x8000;
    uint64 ubuf;
    int type;

    memset(st, 0, sizeof(st));
    if (num == L_fstat) {
      if ((f = lfd(a0)) == 0)
        return -1;
      ubuf = a1;
      ip = f->ip;
    } else {
      char path[MAXPATH];
      if (copyinstr(p->pagetable, path, a1, MAXPATH) < 0)
        return -1;
      ubuf = a2;
      begin_op();
      if ((ip = namei(path)) == 0) {
        end_op();
        return -1;
      }
      ilock(ip);
      type = ip->type;
      size = ip->size;
      iunlockput(ip);
      end_op();
      goto fill;
    }

    if (ip) {
      ilock(ip);
      type = ip->type;
      size = ip->size;
      iunlock(ip);
    } else {
      type = T_DEVICE;
    }

  fill:
    if (type == T_DIR)
      mode = 0x4000;
    else if (type == T_FILE)
      mode = 0x8000;
    else
      mode = 0x2000;
    *(uint32 *)(st + 16) = mode | 0777;
    *(uint32 *)(st + 20) = 1;
    *(uint64 *)(st + 48) = size;
    if (copyout(p->pagetable, ubuf, st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  case L_getcwd: {
    char cwd[2] = "/";
    if (a1 < 2)
      return -1;
    if (copyout(p->pagetable, a0, cwd, 2) < 0)
      return -1;
    return a0;
  }
  case L_setpgid:
    return 0;
  case L_getpgid:
    return p->pid;
  case L_chdir: {
    char path[MAXPATH];
    struct inode *ip;

    if (copyinstr(p->pagetable, path, a0, MAXPATH) < 0)
      return -1;
    begin_op();
    if ((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR) {
      iunlockput(ip);
      end_op();
      return -1;
    }
    iunlock(ip);
    iput(p->cwd);
    end_op();
    p->cwd = ip;
    return 0;
  }
  case L_clone: {
    // 只支持 fork 语义（不含 CLONE_VM）；线程模型暂不支持
    uint64 flags = a0;
    if (flags & 0x100) // CLONE_VM
      return -1;
    return kfork();
  }
  case L_execve: {
    char path[MAXPATH];
    char *argv[MAXARG];
    int i;

    if (copyinstr(p->pagetable, path, a0, MAXPATH) < 0)
      return -1;
    for (i = 0; i < MAXARG - 1; i++) {
      uint64 uarg;
      if (copyin(p->pagetable, (char *)&uarg, a1 + i * sizeof(uint64), sizeof(uarg)) < 0)
        return -1;
      if (uarg == 0)
        break;
      if (copyinstr(p->pagetable, argbuf[i], uarg, MAXPATH) < 0)
        return -1;
      argv[i] = argbuf[i];
    }
    argv[i] = 0;
    if (i == 0)
      return -1;
    return kexec(path, argv);
  }
  case L_prlimit64:
    return 0;
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
