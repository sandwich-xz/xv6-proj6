// 直接用 Linux execve 启动 busybox sh，绕开 xv6 shell 的参数传递。
#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char *argv[3];
  long r;

  argv[0] = "sh";
  argv[1] = "t1.sh";
  argv[2] = 0;

  asm volatile("li a7, 221\n\t"
               "mv a0, %1\n\t"
               "mv a1, %2\n\t"
               "li a2, 0\n\t"
               "ecall\n\t"
               "mv %0, a0"
               : "=r"(r)
               : "r"("/mnt/busybox"), "r"(argv)
               : "a0", "a1", "a2", "a7");
  printf("lxrun: execve returned %ld\n", r);
  exit(0);
}
