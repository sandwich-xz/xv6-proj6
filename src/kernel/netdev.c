// SPDX-License-Identifier: BSD-2-Clause
//
// netdev 注册表：协议栈与具体网卡之间的唯一接口。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "netdev.h"
#include "net.h"

static struct netdev *netdevs[NETDEV_MAX];
static int nnetdev;

int
netdev_register(struct netdev *dev)
{
  if (nnetdev >= NETDEV_MAX)
    return -1;
  netdevs[nnetdev++] = dev;
  return 0;
}

int
netdev_present(void)
{
  return nnetdev > 0;
}

struct netdev *
netdev_first(void)
{
  if (nnetdev == 0)
    return 0;
  return netdevs[0];
}

int
netdev_send(uchar *pkt, uint len)
{
  struct netdev *dev = netdev_first();

  if (dev == 0 || dev->transmit == 0)
    return -1;
  return dev->transmit(dev, pkt, len);
}

void
netdev_input(uchar *pkt, uint len)
{
  ether_input(pkt, len);
}

// MAC 打印成 aa:bb:cc:dd:ee:ff（xv6 的 printf 没有 %02x）
void
net_print_mac(uchar *mac)
{
  int i;

  for (i = 0; i < ETH_ALEN; i++) {
    printf("%x%x", mac[i] >> 4, mac[i] & 0xf);
    if (i != ETH_ALEN - 1)
      printf(":");
  }
}

// 启动时调用：驱动注册后这里会打印 MAC 与 IP。
void
netinit(void)
{
  struct netdev *dev = netdev_first();

  if (dev == 0) {
    printf("net: no netdev\n");
    return;
  }

  printf("net: %s mac ", dev->name);
  net_print_mac(dev->mac);
  printf(" ip %d.%d.%d.%d\n", ((uint)NET_IP >> 24) & 0xff,
         ((uint)NET_IP >> 16) & 0xff, ((uint)NET_IP >> 8) & 0xff,
         (uint)NET_IP & 0xff);

  // 自检：向网关发一个 ARP 请求，收到回复说明收发链路都通了
  arp_request((uint)NET_GW);
}
