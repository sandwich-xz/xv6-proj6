// SPDX-License-Identifier: BSD-2-Clause
//
// IPv4：头部校验、分发、发送。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "netdev.h"
#include "net.h"

static ushort ip_id;

static ushort
ip_cksum(uchar *p, uint len)
{
  uint sum = 0;
  uint i;

  for (i = 0; i + 1 < len; i += 2)
    sum += (p[i] << 8) | p[i + 1];
  if (i < len)
    sum += p[i] << 8;
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return (ushort)(~sum & 0xffff);
}

void
ip_input(uchar *p, uint len)
{
  uint ihl, tot, hdrsum;

  if (len < 20)
    return;
  if ((p[0] >> 4) != 4)
    return;

  ihl = (p[0] & 0xf) * 4;
  if (ihl < 20 || len < ihl)
    return;

  tot = net_be16(p + 2);
  if (tot < ihl || tot > len)
    return;

  hdrsum = ip_cksum(p, ihl);
  if (hdrsum != 0)
    return;

  // 不转发
  if (net_be32(p + 16) != (uint)NET_IP)
    return;

  switch (p[9]) {
  case 1:
    icmp_input(p + ihl, tot - ihl, p);
    break;
  case 17:
    udp_input(p + ihl, tot - ihl, p);
    break;
  default:
    break;
  }
}

// 发送一个 IPv4 包。返回 0 成功，-1 表示没有下一跳 MAC（已发 ARP）。
int
ip_send(uint dst, uchar proto, uchar *payload, uint len)
{
  static uchar pkt[ETH_MTU];
  uchar mac[ETH_ALEN];
  uint tot = 20 + len;

  if (tot > ETH_MTU)
    return -1;
  if (dst != (uint)NET_IP && arp_lookup(dst, mac) != 0) {
    arp_request(dst);
    return -1;
  }

  memset(pkt, 0, 20);
  pkt[0] = 0x45;
  net_put16(pkt + 2, tot);
  net_put16(pkt + 4, ++ip_id);
  pkt[8] = 64;
  pkt[9] = proto;
  net_put32(pkt + 12, (uint)NET_IP);
  net_put32(pkt + 16, dst);
  net_put16(pkt + 10, ip_cksum(pkt, 20));
  memmove(pkt + 20, payload, len);

  // 目的地址是自己：直接回到协议栈，用来做不依赖外部主机的自测
  if (dst == (uint)NET_IP) {
    ip_input(pkt, tot);
    return 0;
  }

  return ether_send(mac, ETH_IP, pkt, tot);
}
