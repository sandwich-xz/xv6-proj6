// SPDX-License-Identifier: BSD-2-Clause
//
// 网络公共定义：链路类型、IP 配置、协议入口。
#ifndef XV6_NET_H
#define XV6_NET_H

#include "types.h"

#define ETH_ALEN 6
#define ETH_HDR_LEN 14
#define ETH_MTU 1500

#define ETH_ARP 0x0806
#define ETH_IP  0x0800

#define ARP_REQUEST 1
#define ARP_REPLY   2

// QEMU user-mode networking 的固定地址
#define NET_IP   0x0a00020fUL // 10.0.2.15
#define NET_GW   0x0a000202UL // 10.0.2.2
#define NET_MASK 0xffffff00UL

// 大端读写（网络字节序）
static inline ushort
net_be16(uchar *p)
{
  return (ushort)((p[0] << 8) | p[1]);
}

static inline uint
net_be32(uchar *p)
{
  return ((uint)p[0] << 24) | ((uint)p[1] << 16) | ((uint)p[2] << 8) | p[3];
}

static inline void
net_put16(uchar *p, ushort v)
{
  p[0] = v >> 8;
  p[1] = v & 0xff;
}

static inline void
net_put32(uchar *p, uint v)
{
  p[0] = v >> 24;
  p[1] = (v >> 16) & 0xff;
  p[2] = (v >> 8) & 0xff;
  p[3] = v & 0xff;
}

void ether_input(uchar *pkt, uint len);
int ether_send(uchar *dst, ushort type, uchar *payload, uint len);

void arp_input(uchar *payload, uint len, uchar *frame);
void arp_request(uint ip);
int arp_lookup(uint ip, uchar *mac);

void ip_input(uchar *payload, uint len);
int ip_send(uint dst, uchar proto, uchar *payload, uint len);

void icmp_input(uchar *payload, uint len, uchar *iphdr);
int icmp_ping(uint dst, ushort seq);

void udp_input(uchar *payload, uint len, uchar *iphdr);
int udp_send(ushort sport, uint dst, ushort dport, uchar *data, uint len);

#endif
