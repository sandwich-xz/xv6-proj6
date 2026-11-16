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

// 启动时调用：驱动注册后这里会打印 MAC 与 IP。
void
netinit(void)
{
  struct netdev *dev = netdev_first();

  if (dev == 0) {
    printf("net: no netdev\n");
    return;
  }
  printf("net: %s mac %x:%x:%x:%x:%x:%x ip %x\n", dev->name, dev->mac[0],
         dev->mac[1], dev->mac[2], dev->mac[3], dev->mac[4], dev->mac[5],
         (uint)NET_IP);
}
