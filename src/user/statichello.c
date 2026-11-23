// 用 Linux ABI（glibc 静态）编译的用户程序，验证内核的 Linux 兼容层。
#include <unistd.h>

int
main(void)
{
  write(1, "static-hello\n", 13);
  write(1, "second-line\n", 12);
  return 0;
}
