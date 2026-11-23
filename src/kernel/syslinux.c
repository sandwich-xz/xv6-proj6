// Linux (riscv64) 系统调用兼容子集。
// 目的：让按 Linux ABI 编译的程序（最终是 busybox 与国赛测例）能在本内核上运行。
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

#define LINUX_SYS_write 64
#define LINUX_SYS_exit 93
#define LINUX_SYS_exit_group 94

uint64
sys_linux(uint64 num)
{
  struct proc *p = myproc();
  uint64 a0 = p->trapframe->a0;
  uint64 a1 = p->trapframe->a1;
  uint64 a2 = p->trapframe->a2;

  switch (num) {
  case LINUX_SYS_write: {
    struct file *f = (a0 < NOFILE) ? p->ofile[a0] : 0;
    if (f == 0)
      return -1;
    return filewrite(f, a1, (int)a2);
  }
  case LINUX_SYS_exit:
  case LINUX_SYS_exit_group:
    kexit((int)a0);
    return 0;
  default:
    printf("linux: unimplemented syscall %ld (pid %d)\n", num, p->pid);
    return -1;
  }
}
