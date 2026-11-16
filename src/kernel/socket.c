// SPDX-License-Identifier: BSD-2-Clause
//
// socket 层：UDP 走队列，TCP 转给 tcb。两者共用 struct file 的 FD_SOCK。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "proc.h"
#include "net.h"
#include "socket.h"
#include "tcp.h"

#define NSOCK 16
#define SOCKQ 8

struct mbuf {
  struct mbuf *next;
  uint srcip;
  ushort sport;
  uint len;
  uchar data[1];
};

struct sock {
  int used;
  int proto; // 0 = UDP, 1 = TCP
  ushort lport;
  struct spinlock lock;
  struct mbuf *head;
  struct mbuf *tail;
  int count;
  struct tcb *tcb;
};

static struct sock socks[NSOCK];

static void
sock_reset(struct sock *s)
{
  initlock(&s->lock, "sock");
  s->used = 1;
  s->head = s->tail = 0;
  s->count = 0;
  s->lport = 0;
  s->tcb = 0;
}

struct sock *
sock_alloc(int proto)
{
  int i;

  for (i = 0; i < NSOCK; i++) {
    if (!socks[i].used) {
      sock_reset(&socks[i]);
      socks[i].proto = proto;
      if (proto == 1) {
        socks[i].tcb = tcp_alloc();
        if (socks[i].tcb == 0) {
          socks[i].used = 0;
          return 0;
        }
      }
      return &socks[i];
    }
  }
  return 0;
}

void
sock_free(struct sock *s)
{
  struct mbuf *m;

  if (s == 0)
    return;
  acquire(&s->lock);
  while ((m = s->head) != 0) {
    s->head = m->next;
    kfree((void *)m);
  }
  s->tail = 0;
  s->count = 0;
  if (s->tcb) {
    tcp_close(s->tcb);
    s->tcb = 0;
  }
  s->used = 0;
  release(&s->lock);
}

int
sock_bind(struct sock *s, ushort port)
{
  int i;

  if (s == 0)
    return -1;
  for (i = 0; i < NSOCK; i++)
    if (socks[i].used && &socks[i] != s && socks[i].lport == port)
      return -1;
  s->lport = port;
  if (s->tcb)
    return tcp_bind(s->tcb, port);
  return 0;
}

ushort
sock_port(struct sock *s)
{
  return s ? s->lport : 0;
}

int
sock_deliver(ushort dport, uint srcip, ushort sport, uchar *data, uint len)
{
  int i;
  struct mbuf *m;

  for (i = 0; i < NSOCK; i++) {
    struct sock *s = &socks[i];

    if (!s->used || s->proto != 0 || s->lport != dport)
      continue;

    acquire(&s->lock);
    if (s->count >= SOCKQ) {
      release(&s->lock);
      return -1;
    }
    release(&s->lock);

    m = (struct mbuf *)kalloc();
    if (m == 0)
      return -1;
    m->next = 0;
    m->srcip = srcip;
    m->sport = sport;
    m->len = len;
    memmove(m->data, data, len);

    acquire(&s->lock);
    if (s->tail)
      s->tail->next = m;
    else
      s->head = m;
    s->tail = m;
    s->count++;
    release(&s->lock);

    wakeup(s);
    return 0;
  }
  return -1;
}

int
sock_recv(struct sock *s, uchar *dst, uint maxlen, uint *srcip, ushort *sport)
{
  struct mbuf *m;
  uint n;

  if (s == 0)
    return -1;

  acquire(&s->lock);
  while (s->head == 0)
    sleep(s, &s->lock);
  m = s->head;
  s->head = m->next;
  if (s->head == 0)
    s->tail = 0;
  s->count--;
  release(&s->lock);

  n = m->len < maxlen ? m->len : maxlen;
  memmove(dst, m->data, n);
  if (srcip)
    *srcip = m->srcip;
  if (sport)
    *sport = m->sport;
  kfree((void *)m);
  return n;
}

int
sock_connect(struct sock *s, uint ip, ushort port)
{
  if (s == 0 || s->tcb == 0)
    return -1;
  return tcp_connect(s->tcb, ip, port);
}

int
sock_listen(struct sock *s)
{
  if (s == 0 || s->tcb == 0)
    return -1;
  return tcp_listen(s->tcb);
}

struct sock *
sock_accept(struct sock *s)
{
  struct tcb *c;
  int i;

  if (s == 0 || s->tcb == 0)
    return 0;
  c = tcp_accept(s->tcb);
  if (c == 0)
    return 0;

  for (i = 0; i < NSOCK; i++) {
    if (!socks[i].used) {
      sock_reset(&socks[i]);
      socks[i].proto = 1;
      socks[i].tcb = c;
      socks[i].lport = s->lport;
      return &socks[i];
    }
  }
  tcp_free(c);
  return 0;
}

// read/write 走 FD_SOCK 时用：TCP 是流，UDP 是数据报。
int
sock_read(struct sock *s, uint64 addr, int n)
{
  static uchar kbuf[ETH_MTU];
  int r;

  if (s == 0 || n <= 0 || n > ETH_MTU)
    return -1;
  if (s->proto == 1)
    r = tcp_recv(s->tcb, kbuf, n);
  else
    r = sock_recv(s, kbuf, n, 0, 0);
  if (r < 0)
    return -1;
  if (copyout(myproc()->pagetable, addr, (char *)kbuf, r) < 0)
    return -1;
  return r;
}

int
sock_write(struct sock *s, uint64 addr, int n)
{
  static uchar kbuf[ETH_MTU];

  if (s == 0 || s->proto != 1 || n <= 0 || n > ETH_MTU)
    return -1;
  if (copyin(myproc()->pagetable, (char *)kbuf, addr, n) < 0)
    return -1;
  return tcp_send(s->tcb, kbuf, n);
}
