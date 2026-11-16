// SPDX-License-Identifier: BSD-2-Clause
//
// 以太网层：只做帧头拼装与类型分发。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "netdev.h"
#include "net.h"

static uchar txbuf[NET_PKT_MAX];

void
ether_input(uchar *pkt, uint len)
{
  ushort type;

  if (len < ETH_HDR_LEN)
    return;

  type = net_be16(pkt + 12);
  switch (type) {
  case ETH_ARP:
    arp_input(pkt + ETH_HDR_LEN, len - ETH_HDR_LEN, pkt);
    break;
  case ETH_IP:
    ip_input(pkt + ETH_HDR_LEN, len - ETH_HDR_LEN);
    break;
  default:
    break;
  }
}

int
ether_send(uchar *dst, ushort type, uchar *payload, uint len)
{
  struct netdev *dev = netdev_first();

  if (dev == 0)
    return -1;
  if (len > ETH_MTU)
    return -1;

  memmove(txbuf, dst, ETH_ALEN);
  memmove(txbuf + ETH_ALEN, dev->mac, ETH_ALEN);
  net_put16(txbuf + 12, type);
  memmove(txbuf + ETH_HDR_LEN, payload, len);

  return netdev_send(txbuf, len + ETH_HDR_LEN);
}
