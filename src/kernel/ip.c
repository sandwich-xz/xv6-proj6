// SPDX-License-Identifier: BSD-2-Clause
//
// IPv4：头部校验、分发、发送。
#include "types.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "netdev.h"
#include "net.h"
#include "tcp.h"

static ushort ip_id;

// 环回队列：目的地址是本机的包不直接递归调用 ip_input，
// 否则会在持有 tcb 锁时重入 tcp_input，导致 acquire 死锁。
// 统一入队，由时钟中断里 net_loopback_poll() 投递。
#define LBQ 16
static struct {
  uchar buf[1600];
  uint len;
} lbq[LBQ];
static int lbhead, lbtail;
static struct spinlock lblk;
static int lbinit;

void
net_loopback_init(void)
{
  if (!lbinit) {
    initlock(&lblk, "loopback");
    lbinit = 1;
  }
}

static void
net_loopback_enqueue(uchar *p, uint len)
{
  net_loopback_init();
  acquire(&lblk);
  if ((lbhead + 1) % LBQ == lbtail) {
    release(&lblk);
    return;
  }
  memmove(lbq[lbhead].buf, p, len);
  lbq[lbhead].len = len;
  lbhead = (lbhead + 1) % LBQ;
  release(&lblk);
}

void
net_loopback_poll(void)
{
  // 这里不能用 1600 字节的局部缓冲：该函数既会在系统调用里被调用，
  // 也会在时钟中断里被调用，嵌套时 1.6KB 的栈帧足以把 8KB 内核栈压爆
  // （表现为 kernelvec 上二次异常）。改成静态缓冲 + 串行锁。
  static uchar buf[1600];
  static struct spinlock poll_lock;
  static int poll_init;
  uint len;

  if (!lbinit)
    return;
  if (!poll_init) {
    initlock(&poll_lock, "lbpoll");
    poll_init = 1;
  }

  acquire(&poll_lock);
  for (;;) {
    acquire(&lblk);
    if (lbtail == lbhead) {
      release(&lblk);
      release(&poll_lock);
      return;
    }
    len = lbq[lbtail].len;
    memmove(buf, lbq[lbtail].buf, len);
    lbtail = (lbtail + 1) % LBQ;
    release(&lblk);
    ip_input(buf, len);
  }
}

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
  case 6:
    tcp_input(p + ihl, tot - ihl, p);
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

  // 目的地址是自己：入队，由时钟中断统一投递（不能同步递归）
  if (dst == (uint)NET_IP) {
    net_loopback_enqueue(pkt, tot);
    return 0;
  }

  return ether_send(mac, ETH_IP, pkt, tot);
}
