// SPDX-License-Identifier: BSD-2-Clause
//
// IPv4 入口。当前只做最小校验，UDP/TCP 分发在后续提交里补。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "net.h"

void
ip_input(uchar *p, uint len)
{
  uint ihl;

  if (len < 20)
    return;
  if ((p[0] >> 4) != 4)
    return;

  ihl = (p[0] & 0xf) * 4;
  if (ihl < 20 || len < ihl)
    return;

  // 目的地址不是我们，直接丢（没有转发功能）
  if (net_be32(p + 16) != (uint)NET_IP)
    return;

  switch (p[9]) {
  case 1:  // ICMP
  case 6:  // TCP
  case 17: // UDP
    // TODO: 分发到对应协议
    break;
  default:
    break;
  }
}
