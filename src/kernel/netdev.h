// SPDX-License-Identifier: BSD-2-Clause
//
// 与 block.h 同级的薄设备抽象。
// 协议栈只认 netdev，不认 virtio 还是 e1000；架构差异收敛在 transmit 里。
#ifndef XV6_NETDEV_H
#define XV6_NETDEV_H

#include "types.h"

#define NETDEV_MAX 2
#define NET_PKT_MAX 1514

struct netdev {
  char name[8];
  uchar mac[6];
  int (*transmit)(struct netdev *dev, uchar *pkt, uint len);
};

int netdev_register(struct netdev *dev);
int netdev_present(void);
struct netdev *netdev_first(void);
int netdev_send(uchar *pkt, uint len);

// 驱动收到一帧后调用（中断或轮询上下文），只把包交给链路层。
void netdev_input(uchar *pkt, uint len);

#endif
