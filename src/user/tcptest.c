// SPDX-License-Identifier: BSD-2-Clause
//
// TCP 自测：父进程 listen，子进程 connect（都指向本机地址），
// 然后双向各发一段数据。走内核环回，不依赖外部主机。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define LOCAL_IP 0x0a00020f // 10.0.2.15
#define PORT 8000

int
main(void)
{
  int ls, fd, n, pid;
  char buf[64];

  ls = socket(2, 1, 0); // AF_INET, SOCK_STREAM
  if (ls < 0) {
    printf("tcptest: socket failed\n");
    exit(1);
  }
  if (bind(ls, PORT) < 0) {
    printf("tcptest: bind failed\n");
    exit(1);
  }
  if (listen(ls) < 0) {
    printf("tcptest: listen failed\n");
    exit(1);
  }

  pid = fork();
  if (pid == 0) {
    int c = socket(2, 1, 0);
    if (c < 0) {
      printf("tcptest: child socket failed\n");
      exit(1);
    }
    if (connect(c, LOCAL_IP, PORT) < 0) {
      printf("tcptest: connect failed\n");
      exit(1);
    }
    if (write(c, "ping", 4) != 4) {
      printf("tcptest: child write failed\n");
      exit(1);
    }
    memset(buf, 0, sizeof(buf));
    n = read(c, buf, sizeof(buf) - 1);
    printf("tcptest: client got %d bytes [%s]\n", n, n > 0 ? buf : "");
    close(c);
    exit(0);
  }

  fd = accept(ls);
  if (fd < 0) {
    printf("tcptest: accept failed\n");
    exit(1);
  }
  memset(buf, 0, sizeof(buf));
  n = read(fd, buf, sizeof(buf) - 1);
  printf("tcptest: server got %d bytes [%s]\n", n, n > 0 ? buf : "");
  write(fd, "pong", 4);
  close(fd);
  close(ls);
  wait(0);
  exit(0);
}
