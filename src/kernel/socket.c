// SPDX-License-Identifier: BSD-2-Clause
//
// UDP socket 层：把 fd 语义接到内核的 UDP 上。
// 队列里存的是已经付过 kalloc 的数据报，读取时拷出并释放。
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
  ushort lport;
  uint rip;
  ushort rport;
  struct spinlock lock;
  struct mbuf *head;
  struct mbuf *tail;
  int count;
};

static struct sock socks[NSOCK];

int
sockinit_slot(struct sock *s)
{
  initlock(&s->lock, "sock");
  s->used = 1;
  s->head = s->tail = 0;
  s->count = 0;
  s->lport = 0;
  return 0;
}

struct sock *
sock_alloc(void)
{
  int i;

  for (i = 0; i < NSOCK; i++) {
    if (!socks[i].used) {
      sockinit_slot(&socks[i]);
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
  s->used = 0;
  release(&s->lock);
}

int
sock_bind(struct sock *s, ushort port){
  int i;

  if (s == 0)
    return -1;
  for (i = 0; i < NSOCK; i++)
    if (socks[i].used && &socks[i] != s && socks[i].lport == port)
      return -1;
  acquire(&s->lock);
  s->lport = port;
  release(&s->lock);
  return 0;
}

ushort
sock_port(struct sock *s)
{
  return s ? s->lport : 0;
}

// 收包：按目的端口找到 socket，挂到队列并唤醒读者。
int
sock_deliver(ushort dport, uint srcip, ushort sport, uchar *data, uint len)
{
  int i;
  struct mbuf *m;

  for (i = 0; i < NSOCK; i++) {
    struct sock *s = &socks[i];

    if (!s->used || s->lport != dport)
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

// 阻塞读一个数据报。返回长度，失败返回 -1。
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
