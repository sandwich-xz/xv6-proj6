// SPDX-License-Identifier: BSD-2-Clause
#ifndef XV6_SOCKET_H
#define XV6_SOCKET_H

#include "types.h"

struct sock;

struct sock *sock_alloc(int proto);
void sock_free(struct sock *s);
int sock_bind(struct sock *s, ushort port);
ushort sock_port(struct sock *s);
int sock_deliver(ushort dport, uint srcip, ushort sport, uchar *data, uint len);
int sock_recv(struct sock *s, uchar *dst, uint maxlen, uint *srcip, ushort *sport);

int sock_connect(struct sock *s, uint ip, ushort port);
int sock_listen(struct sock *s);
struct sock *sock_accept(struct sock *s);
int sock_read(struct sock *s, uint64 addr, int n);
int sock_write(struct sock *s, uint64 addr, int n);

#endif
