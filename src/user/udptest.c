// SPDX-License-Identifier: BSD-2-Clause
//
// UDP 自测：绑定本地端口，向本机地址发一个数据报，再收回来。
// 走的是内核里的环回路径（目的地址 = 本机），不依赖外部主机。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define LOCAL_IP 0x0a00020f // 10.0.2.15
#define PORT 7777

int
main(void)
{
  int fd, n, ip = 0, port = 0;
  char msg[] = "hello-xv6-udp";
  char buf[64];

  fd = socket(2, 2, 0);
  if (fd < 0) {
    printf("udptest: socket failed\n");
    exit(1);
  }
  if (bind(fd, PORT) < 0) {
    printf("udptest: bind failed\n");
    exit(1);
  }

  if (sendto(fd, LOCAL_IP, PORT, msg, strlen(msg)) < 0) {
    printf("udptest: sendto failed\n");
    exit(1);
  }

  memset(buf, 0, sizeof(buf));
  n = recvfrom(fd, buf, sizeof(buf) - 1, &ip, &port);
  if (n < 0) {
    printf("udptest: recvfrom failed\n");
    exit(1);
  }

  printf("udptest: recv %d bytes from %d.%d.%d.%d:%d [%s]\n", n,
         (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
         port, buf);

  close(fd);
  exit(0);
}
