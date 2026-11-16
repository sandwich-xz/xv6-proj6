// SPDX-License-Identifier: BSD-2-Clause
#ifndef XV6_SOCKET_H
#define XV6_SOCKET_H

#include "types.h"

struct sock;

struct sock *sock_alloc(void);
void sock_free(struct sock *s);
int sock_bind(struct sock *s, ushort port);
ushort sock_port(struct sock *s);
int sock_deliver(ushort dport, uint srcip, ushort sport, uchar *data, uint len);
int sock_recv(struct sock *s, uchar *dst, uint maxlen, uint *srcip, ushort *sport);

#endif
