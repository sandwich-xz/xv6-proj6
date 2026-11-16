// SPDX-License-Identifier: BSD-2-Clause
//
// ICMP：先只做 echo，够 ping 用。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "netdev.h"
#include "net.h"

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO 8

#ifdef ARCH_RISCV
static uint64 icmp_t0;
#endif

static ushort icmp_cksum(uchar *p, uint len)
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
icmp_input(uchar *p, uint len, uchar *iphdr)
{
  uint src = net_be32(iphdr + 12);
  uchar reply[64];

  if (len < 8)
    return;

  if (p[0] == ICMP_ECHO && p[1] == 0) {
    if (len > sizeof(reply))
      len = sizeof(reply);
    memmove(reply, p, len);
    reply[0] = ICMP_ECHO_REPLY;
    reply[2] = 0;
    reply[3] = 0;
    net_put16(reply + 2, icmp_cksum(reply, len));
    ip_send(src, 1, reply, len);
    return;
  }

  if (p[0] == ICMP_ECHO_REPLY && p[1] == 0) {
#ifdef ARCH_RISCV
    uint64 rtt = r_time() - icmp_t0;
    printf("ping: %d.%d.%d.%d seq=%d ok rtt=%ld ticks\n", (src >> 24) & 0xff,
           (src >> 16) & 0xff, (src >> 8) & 0xff, src & 0xff,
           net_be16(p + 6), rtt);
#else
    printf("ping: %d.%d.%d.%d seq=%d ok\n", (src >> 24) & 0xff,
           (src >> 16) & 0xff, (src >> 8) & 0xff, src & 0xff,
           net_be16(p + 6));
#endif
  }
}

int
icmp_ping(uint dst, ushort seq)
{
  uchar req[16];

  memset(req, 0, sizeof(req));
  req[0] = ICMP_ECHO;
  req[4] = 0x12;
  req[5] = 0x34;
  net_put16(req + 6, seq);
  net_put16(req + 2, icmp_cksum(req, sizeof(req)));

#ifdef ARCH_RISCV
  icmp_t0 = r_time();
#endif
  return ip_send(dst, 1, req, sizeof(req));
}
