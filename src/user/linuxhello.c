// 用 Linux ABI 直接发系统调用，验证内核的 Linux 号分流。
// 不经过 xv6 的 usys：a7 里放的是 Linux 号（64=write, 93=exit）。
#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char *msg = "linux-hello\n";
  long r;

  asm volatile("li a7, 64\n\t"
               "li a0, 1\n\t"
               "mv a1, %1\n\t"
               "li a2, 12\n\t"
               "ecall\n\t"
               "mv %0, a0"
               : "=r"(r)
               : "r"(msg)
               : "a0", "a1", "a2", "a7");
  printf("linux: write returned %ld\n", r);

  asm volatile("li a7, 93\n\t"
               "li a0, 0\n\t"
               "ecall" ::: "a0", "a7");
  printf("linux: should not reach here\n");
  exit(0);
}
