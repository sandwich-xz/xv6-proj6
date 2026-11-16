// SPDX-License-Identifier: BSD-2-Clause
//
// 最小 TCP：三次握手、停等发送（一次一段）、累计 ACK、超时重传、四次挥手。
// 刻意不做：滑动窗口、SACK、时间戳、乱序重组。先把"正确"拿下，性能后面再谈。
#ifndef XV6_TCP_H
#define XV6_TCP_H

#include "types.h"

struct tcb;

struct tcb *tcp_alloc(void);
void tcp_free(struct tcb *t);
int tcp_bind(struct tcb *t, ushort port);
int tcp_listen(struct tcb *t);
int tcp_connect(struct tcb *t, uint ip, ushort port);
struct tcb *tcp_accept(struct tcb *t);
int tcp_send(struct tcb *t, uchar *buf, uint len);
int tcp_recv(struct tcb *t, uchar *buf, uint len);
void tcp_close(struct tcb *t);

void tcp_input(uchar *p, uint len, uchar *iphdr);
void tcp_timer(void);

#endif
