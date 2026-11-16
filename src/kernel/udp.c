// SPDX-License-Identifier: BSD-2-Clause
//
// UDP。下一阶段接 socket 层；现在先占位，保证 ip.c 的分发能链接。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "net.h"

void
udp_input(uchar *p, uint len, uchar *iphdr)
{
  (void)p;
  (void)len;
  (void)iphdr;
}
