// SPDX-License-Identifier: BSD-2-Clause
//
// 最小 TCP 实现。状态机够跑通握手/收发/挥手，重传按最朴素的超时重发。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"
#include "netdev.h"
#include "tcp.h"

#define TCP_MSS 1000
#define TCP_RXBUF 8192
#define TCP_NTcb 8
#define TCP_ACCEPTQ 4

#define F_FIN 0x01
#define F_SYN 0x02
#define F_RST 0x04
#define F_PSH 0x08
#define F_ACK 0x10

#define T_CLOSED 0
#define T_LISTEN 1
#define T_SYN_SENT 2
#define T_SYN_RCVD 3
#define T_ESTAB 4
#define T_FIN_WAIT1 5
#define T_FIN_WAIT2 6
#define T_CLOSE_WAIT 7
#define T_LAST_ACK 8
#define T_TIME_WAIT 9

#define REXMIT_TICKS 30
#define MAX_TRIES 6

extern uint ticks;

struct tcb {
  int used;
  int state;
  ushort lport;
  ushort rport;
  uint rip;

  uint iss;
  uint snd_nxt;
  uint snd_una;
  uint rcv_nxt;

  uchar *rx;
  uint rxlen;

  uchar *tx;
  uint txlen;
  uint txseq;
  int txflags;
  int tx_pending;
  int tx_time;
  int tx_tries;

  struct spinlock lock;

  struct tcb *acceptq[TCP_ACCEPTQ];
  int naq;
};

static struct tcb tcbs[TCP_NTcb];

static ushort
tcp_cksum(uchar *p, uint len, uint src, uint dst)
{
  uchar ph[12];
  uint sum = 0;
  uint i;

  net_put32(ph + 0, src);
  net_put32(ph + 4, dst);
  ph[8] = 0;
  ph[9] = 6;
  net_put16(ph + 10, len);

  for (i = 0; i + 1 < 12; i += 2)
    sum += (ph[i] << 8) | ph[i + 1];
  for (i = 0; i + 1 < len; i += 2)
    sum += (p[i] << 8) | p[i + 1];
  if (i < len)
    sum += p[i] << 8;
  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);
  return (ushort)(~sum & 0xffff);
}

struct tcb *
tcp_alloc(void)
{
  int i;

  for (i = 0; i < TCP_NTcb; i++) {
    if (!tcbs[i].used) {
      struct tcb *t = &tcbs[i];

      memset(t, 0, sizeof(*t));
      initlock(&t->lock, "tcb");
      t->rx = (uchar *)kalloc();
      t->tx = (uchar *)kalloc();
      if (t->rx == 0 || t->tx == 0)
        return 0;
      t->used = 1;
      t->state = T_CLOSED;
      return t;
    }
  }
  return 0;
}

void
tcp_free(struct tcb *t)
{
  if (t == 0)
    return;
  acquire(&t->lock);
  t->state = T_CLOSED;
  t->used = 0;
  if (t->rx) {
    kfree(t->rx);
    t->rx = 0;
  }
  if (t->tx) {
    kfree(t->tx);
    t->tx = 0;
  }
  release(&t->lock);
}

int
tcp_bind(struct tcb *t, ushort port)
{
  int i;

  for (i = 0; i < TCP_NTcb; i++)
    if (tcbs[i].used && &tcbs[i] != t && tcbs[i].lport == port &&
        (tcbs[i].state == T_LISTEN || tcbs[i].state == T_ESTAB))
      return -1;
  t->lport = port;
  return 0;
}

static int
tcp_send_seg(struct tcb *t, uchar flags, uchar *data, uint len)
{
  static uchar pkt[ETH_MTU];
  uint seq = t->snd_nxt;

  if (20 + len > ETH_MTU)
    return -1;

  net_put16(pkt + 0, t->lport);
  net_put16(pkt + 2, t->rport);
  net_put32(pkt + 4, seq);
  net_put32(pkt + 8, t->rcv_nxt);
  pkt[12] = (5 << 4);
  pkt[13] = flags;
  net_put16(pkt + 14, 4096);
  net_put16(pkt + 16, 0);
  net_put16(pkt + 18, 0);
  memmove(pkt + 20, data, len);
  net_put16(pkt + 16, tcp_cksum(pkt, 20 + len, (uint)NET_IP, t->rip));

  if (ip_send(t->rip, 6, pkt, 20 + len) < 0)
    return -1;

  t->snd_nxt += len;
  if (flags & (F_SYN | F_FIN))
    t->snd_nxt += 1;
  return 0;
}

static void
tcp_tx_track(struct tcb *t, uint len, uchar flags)
{
  t->txseq = t->snd_nxt - len - ((flags & (F_SYN | F_FIN)) ? 1 : 0);
  t->txlen = len;
  t->txflags = flags;
  t->tx_pending = 1;
  t->tx_time = ticks;
  t->tx_tries = 0;
}

int
tcp_connect(struct tcb *t, uint ip, ushort port)
{
  t->rip = ip;
  t->rport = port;
  t->iss = (uint)ticks * 64000 + t->lport;
  t->snd_nxt = t->iss;
  t->snd_una = t->iss;
  t->state = T_SYN_SENT;

  acquire(&t->lock);
  if (tcp_send_seg(t, F_SYN, 0, 0) < 0) {
    release(&t->lock);
    return -1;
  }
  tcp_tx_track(t, 0, F_SYN);
  while (t->state != T_ESTAB && t->state != T_CLOSED)
    sleep(t, &t->lock);
  release(&t->lock);
  return t->state == T_ESTAB ? 0 : -1;
}

int
tcp_listen(struct tcb *t)
{
  t->state = T_LISTEN;
  return 0;
}

struct tcb *
tcp_accept(struct tcb *t)
{
  struct tcb *c;

  acquire(&t->lock);
  while (t->naq == 0)
    sleep(t, &t->lock);
  c = t->acceptq[0];
  for (int i = 1; i < t->naq; i++)
    t->acceptq[i - 1] = t->acceptq[i];
  t->naq--;
  release(&t->lock);
  return c;
}

// 等 ACK 期间也要把环回队列投递出去：否则停等发送只能等时钟中断，
// 环回吞吐会退化到几 KB/s（实测 8KB/1.49s）。放开锁再投递，避免重入死锁。
static void
tcp_wait_ack(struct tcb *t)
{
  while (t->tx_pending && t->state != T_CLOSED) {
    release(&t->lock);
    net_loopback_poll();
    acquire(&t->lock);
    if (!t->tx_pending || t->state == T_CLOSED)
      break;
    sleep(t, &t->lock);
  }
}

int
tcp_send(struct tcb *t, uchar *buf, uint len)
{
  uint off = 0;

  acquire(&t->lock);
  while (off < len) {
    uint n = len - off;

    if (n > TCP_MSS)
      n = TCP_MSS;
    tcp_wait_ack(t);
    if (t->state == T_CLOSED) {
      release(&t->lock);
      return -1;
    }
    if (tcp_send_seg(t, F_ACK | F_PSH, buf + off, n) < 0) {
      release(&t->lock);
      return -1;
    }
    tcp_tx_track(t, n, F_ACK | F_PSH);
    tcp_wait_ack(t);
    off += n;
  }
  release(&t->lock);
  return len;
}

int
tcp_recv(struct tcb *t, uchar *buf, uint len)
{
  uint n;

  acquire(&t->lock);
  while (t->rxlen == 0 && t->state != T_CLOSE_WAIT && t->state != T_CLOSED)
    sleep(t, &t->lock);
  if (t->rxlen == 0 && t->state == T_CLOSED) {
    release(&t->lock);
    return -1;
  }
  n = t->rxlen < len ? t->rxlen : len;
  memmove(buf, t->rx, n);
  t->rxlen -= n;
  memmove(t->rx, t->rx + n, t->rxlen);
  release(&t->lock);
  return n;
}

void
tcp_close(struct tcb *t)
{
  acquire(&t->lock);
  if (t->state == T_ESTAB) {
    tcp_send_seg(t, F_FIN | F_ACK, 0, 0);
    tcp_tx_track(t, 0, F_FIN | F_ACK);
    t->state = T_FIN_WAIT1;
  } else if (t->state == T_CLOSE_WAIT) {
    tcp_send_seg(t, F_FIN | F_ACK, 0, 0);
    tcp_tx_track(t, 0, F_FIN | F_ACK);
    t->state = T_LAST_ACK;
  } else if (t->state == T_LISTEN) {
    t->state = T_CLOSED;
  }
  release(&t->lock);
}

static struct tcb *
tcp_find(ushort lport, ushort rport, uint rip)
{
  int i;

  for (i = 0; i < TCP_NTcb; i++) {
    struct tcb *t = &tcbs[i];

    if (!t->used)
      continue;
    if (t->lport != lport)
      continue;
    if (t->state == T_LISTEN)
      continue;
    if (t->rport == rport && t->rip == rip)
      return t;
  }
  return 0;
}

static struct tcb *
tcp_listener(ushort lport)
{
  int i;

  for (i = 0; i < TCP_NTcb; i++)
    if (tcbs[i].used && tcbs[i].state == T_LISTEN && tcbs[i].lport == lport)
      return &tcbs[i];
  return 0;
}

static void
tcp_ack_all(struct tcb *t, uint ack)
{
  if (ack > t->snd_una)
    t->snd_una = ack;
  if (t->tx_pending && ack >= t->txseq + t->txlen + ((t->txflags & (F_SYN | F_FIN)) ? 1 : 0)) {
    t->tx_pending = 0;
    wakeup(t);
  }
}

void
tcp_input(uchar *p, uint len, uchar *iphdr)
{
  ushort sport, dport, wnd;
  uint seq, ack, src;
  uchar flags;
  uint doff;
  struct tcb *t;
  uint datalen;

  if (len < 20)
    return;

  sport = net_be16(p + 0);
  dport = net_be16(p + 2);
  seq = net_be32(p + 4);
  ack = net_be32(p + 8);
  doff = (p[12] >> 4) * 4;
  flags = p[13];
  wnd = net_be16(p + 14);
  src = net_be32(iphdr + 12);
  (void)wnd;

  if (doff < 20 || len < doff)
    return;
  if (tcp_cksum(p, len, src, (uint)NET_IP) != 0)
    return;

  datalen = len - doff;

  t = tcp_find(dport, sport, src);
  if (t == 0) {
    struct tcb *l = tcp_listener(dport);

    if (l == 0)
      return;
    if (!(flags & F_SYN) || (flags & F_ACK))
      return;

    t = tcp_alloc();
    if (t == 0)
      return;

    acquire(&l->lock);
    t->lport = dport;
    t->rip = src;
    t->rport = sport;
    t->rcv_nxt = seq + 1;
    t->iss = (uint)ticks * 64000 + dport + 7;
    t->snd_nxt = t->iss;
    t->snd_una = t->iss;
    t->state = T_SYN_RCVD;

    acquire(&t->lock);
    tcp_send_seg(t, F_SYN | F_ACK, 0, 0);
    tcp_tx_track(t, 0, F_SYN | F_ACK);
    release(&t->lock);

    if (l->naq < TCP_ACCEPTQ)
      l->acceptq[l->naq++] = t;
    wakeup(l);
    release(&l->lock);
    return;
  }

  acquire(&t->lock);

  if (t->state == T_SYN_SENT) {
    if ((flags & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == t->snd_nxt) {
      t->rcv_nxt = seq + 1;
      t->snd_una = ack;
      t->tx_pending = 0;
      t->state = T_ESTAB;
      tcp_send_seg(t, F_ACK, 0, 0);
      wakeup(t);
    }
    release(&t->lock);
    return;
  }

  if (flags & F_ACK)
    tcp_ack_all(t, ack);

  if (flags & F_RST) {
    t->state = T_CLOSED;
    wakeup(t);
    release(&t->lock);
    return;
  }

  if ((flags & F_SYN) && t->state == T_SYN_RCVD) {
    if (ack == t->snd_nxt) {
      t->state = T_ESTAB;
      wakeup(t);
    }
    release(&t->lock);
    return;
  }

  if (datalen > 0 && seq == t->rcv_nxt) {
    uint n = datalen;

    if (t->rxlen + n > TCP_RXBUF)
      n = TCP_RXBUF - t->rxlen;
    memmove(t->rx + t->rxlen, p + doff, n);
    t->rxlen += n;
    t->rcv_nxt += datalen;
    tcp_send_seg(t, F_ACK, 0, 0);
    wakeup(t);
  } else if (datalen > 0) {
    tcp_send_seg(t, F_ACK, 0, 0); // 乱序先丢弃，回 ACK 让对端重发
  }

  if (flags & F_FIN) {
    t->rcv_nxt += 1;
    tcp_send_seg(t, F_ACK, 0, 0);
    if (t->state == T_ESTAB)
      t->state = T_CLOSE_WAIT;
    else if (t->state == T_FIN_WAIT2)
      t->state = T_TIME_WAIT;
    wakeup(t);
  }

  if (t->state == T_FIN_WAIT1 && t->tx_pending == 0)
    t->state = T_FIN_WAIT2;

  release(&t->lock);
}

void
tcp_timer(void)
{
  int i;

  for (i = 0; i < TCP_NTcb; i++) {
    struct tcb *t = &tcbs[i];

    if (!t->used || !t->tx_pending)
      continue;
    if (ticks - t->tx_time < REXMIT_TICKS)
      continue;
    if (++t->tx_tries > MAX_TRIES) {
      t->tx_pending = 0;
      t->state = T_CLOSED;
      wakeup(t);
      continue;
    }
    t->tx_time = ticks;
    t->snd_nxt = t->txseq;
    tcp_send_seg(t, t->txflags, t->tx, t->txlen);
  }
}
