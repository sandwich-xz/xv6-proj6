// SPDX-License-Identifier: BSD-2-Clause
//
// 动态链接器（第一步）：内核把我们映射到 0x30000000 并从 auxv 传入口地址。
// 这一版只验证 exec/PT_INTERP/auxv 链路：打印参数，然后跳到主程序入口。
// 真正的库加载与重定位下一步再加。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define AT_NULL 0
#define AT_BASE 7
#define AT_ENTRY 9

int
main(int argc, char **argv)
{
  uint64 *auxv = (uint64 *)(argv + argc + 1);
  uint64 entry = 0, base = 0;
  int i;

  for (i = 0; auxv[i] != AT_NULL && i < 16; i += 2) {
    if (auxv[i] == AT_ENTRY)
      entry = auxv[i + 1];
    else if (auxv[i] == AT_BASE)
      base = auxv[i + 1];
  }

  printf("ld.so: argc=%d argv0=%s base=0x%x entry=0x%x\n", argc,
         argc > 0 ? argv[0] : "", (uint)base, (uint)entry);

  if (entry == 0) {
    printf("ld.so: no AT_ENTRY\n");
    exit(1);
  }

  // 这版主程序不依赖外部符号，直接交棒
  int (*start)(int, char **) = (int (*)(int, char **))entry;
  exit(start(argc, argv));
}
