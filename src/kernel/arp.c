// SPDX-License-Identifier: BSD-2-Clause
//
// ARP：把网关与对端的 IP 解析成 MAC。缓存很小，线性查。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "netdev.h"
#include "net.h"

#define ARP_NENTRY 8
#define ARP_LEN 28

struct arp_entry {
  uint ip;
  uchar mac[ETH_ALEN];
  int valid;
};

static struct arp_entry arp_table[ARP_NENTRY];
static uchar bcast[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static void
arp_update(uint ip, uchar *mac)
{
  int i, slot = -1;

  for (i = 0; i < ARP_NENTRY; i++) {
    if (arp_table[i].valid && arp_table[i].ip == ip) {
      memmove(arp_table[i].mac, mac, ETH_ALEN);
      return;
    }
    if (!arp_table[i].valid && slot < 0)
      slot = i;
  }
  if (slot < 0)
    slot = 0;
  arp_table[slot].ip = ip;
  memmove(arp_table[slot].mac, mac, ETH_ALEN);
  arp_table[slot].valid = 1;
}

int
arp_lookup(uint ip, uchar *mac)
{
  int i;

  for (i = 0; i < ARP_NENTRY; i++) {
    if (arp_table[i].valid && arp_table[i].ip == ip) {
      memmove(mac, arp_table[i].mac, ETH_ALEN);
      return 0;
    }
  }
  return -1;
}

// 发一个 ARP 请求（广播）。
void
arp_request(uint ip)
{
  uchar req[ARP_LEN];
  struct netdev *dev = netdev_first();

  if (dev == 0)
    return;

  memset(req, 0, sizeof(req));
  net_put16(req + 0, 1);      // htype: ethernet
  net_put16(req + 2, ETH_IP); // ptype
  req[4] = ETH_ALEN;
  req[5] = 4;
  net_put16(req + 6, ARP_REQUEST);
  memmove(req + 8, dev->mac, ETH_ALEN);
  net_put32(req + 14, (uint)NET_IP);
  net_put32(req + 24, ip);

  ether_send(bcast, ETH_ARP, req, sizeof(req));
}

void
arp_input(uchar *p, uint len, uchar *frame)
{
  ushort op;
  uchar sha[ETH_ALEN];
  uint spa, tpa;

  (void)frame;
  if (len < ARP_LEN)
    return;

  op = net_be16(p + 6);
  memmove(sha, p + 8, ETH_ALEN);
  spa = net_be32(p + 14);
  tpa = net_be32(p + 24);

  if (spa != (uint)NET_IP)
    arp_update(spa, sha);

  if (op == ARP_REQUEST && tpa == (uint)NET_IP) {
    struct netdev *dev = netdev_first();
    uchar rep[ARP_LEN];

    if (dev == 0)
      return;

    memset(rep, 0, sizeof(rep));
    net_put16(rep + 0, 1);
    net_put16(rep + 2, ETH_IP);
    rep[4] = ETH_ALEN;
    rep[5] = 4;
    net_put16(rep + 6, ARP_REPLY);
    memmove(rep + 8, dev->mac, ETH_ALEN);
    net_put32(rep + 14, (uint)NET_IP);
    memmove(rep + 18, sha, ETH_ALEN);
    net_put32(rep + 24, spa);

    ether_send(sha, ETH_ARP, rep, sizeof(rep));
  }
}
