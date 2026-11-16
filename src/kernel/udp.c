// SPDX-License-Identifier: BSD-2-Clause
//
// UDP：头部解析、伪首部校验、交给 socket 层。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "net.h"
#include "socket.h"

static uint
sum16(uchar *p, uint len, uint sum)
{
  uint i;

  for (i = 0; i + 1 < len; i += 2)
    sum += (p[i] << 8) | p[i + 1];
  if (i < len)
    sum += p[i] << 8;
  return sum;
}

static ushort
fold(uint sum)
{
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return (ushort)(~sum & 0xffff);
}

// UDP 伪首部：源地址 4 + 目的地址 4 + 零 1 + 协议 1 + UDP 长度 2
static uint
pseudo_sum(uint src, uint dst, ushort ulen, uint sum)
{
  uchar ph[12];

  net_put32(ph + 0, src);
  net_put32(ph + 4, dst);
  ph[8] = 0;
  ph[9] = 17;
  net_put16(ph + 10, ulen);
  return sum16(ph, 12, sum);
}

void
udp_input(uchar *p, uint len, uchar *iphdr)
{
  ushort sport, dport, ulen;
  uint src;

  if (len < 8)
    return;

  sport = net_be16(p + 0);
  dport = net_be16(p + 2);
  ulen = net_be16(p + 4);
  if (ulen < 8 || ulen > len)
    return;

  // 校验和（0 表示未计算，按 RFC 允许）
  if (net_be16(p + 6) != 0) {
    uint sum = pseudo_sum(net_be32(iphdr + 12), (uint)NET_IP, ulen, 0);
    sum = sum16(p, ulen, sum);
    if (fold(sum) != 0)
      return;
  }

  src = net_be32(iphdr + 12);
  sock_deliver(dport, src, sport, p + 8, ulen - 8);
}

int
udp_send(ushort sport, uint dst, ushort dport, uchar *data, uint len)
{
  static uchar pkt[ETH_MTU];
  uint ulen = 8 + len;
  uint sum;

  if (ulen > ETH_MTU - 20)
    return -1;

  net_put16(pkt + 0, sport);
  net_put16(pkt + 2, dport);
  net_put16(pkt + 4, ulen);
  net_put16(pkt + 6, 0);
  memmove(pkt + 8, data, len);

  sum = pseudo_sum((uint)NET_IP, dst, ulen, 0);
  sum = sum16(pkt, ulen, sum);
  net_put16(pkt + 6, fold(sum));

  return ip_send(dst, 17, pkt, ulen);
}
