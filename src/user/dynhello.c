// SPDX-License-Identifier: BSD-2-Clause
//
// 带 PT_INTERP 的主程序：由 ld.so 接手后跳回这里。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char **argv)
{
  printf("dynhello: hello from a dynamic executable, argc=%d argv0=%s\n", argc,
         argc > 0 ? argv[0] : "");
  exit(0);
}
