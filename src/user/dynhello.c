// SPDX-License-Identifier: BSD-2-Clause
//
// 带 PT_INTERP 的主程序：由 ld.so 接手、填好 JUMP_SLOT 后跳回这里。
// 这里真的调用共享库里的 add()，验证重定位是生效的。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int add(int, int);

int
main(int argc, char **argv)
{
  int r = add(2, 3);

  printf("dynhello: add(2,3)=%d argc=%d argv0=%s\n", r, argc,
         argc > 0 ? argv[0] : "");
  exit(0);
}
