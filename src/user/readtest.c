// 复现 ld.so 的读取顺序：先读主程序，再读共享库。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static char buf[8192];

int
main(void)
{
  char *names[2] = {"dynhello", "libmath.so"};

  for (int round = 0; round < 2; round++) {
    int fd = open(names[round], 0);
    if (fd < 0) {
      printf("readtest: open %s failed\n", names[round]);
      exit(1);
    }
    int total = 0, n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
      total += n;
    close(fd);
    printf("readtest: %s total=%d\n", names[round], total);
  }
  printf("readtest: done\n");
  exit(0);
}
