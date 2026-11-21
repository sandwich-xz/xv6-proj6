// 网络基准：UDP 往返延迟与 TCP 吞吐（走内核环回）。
// 时间用 rdtime() 读 10MHz 时间计数器，换算成微秒。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define LOCAL_IP 0x0a00020f // 10.0.2.15
#define PORT 7100

static char msg[64];
static char buf[1024];

static void
bench_udp(void)
{
  int fd = socket(2, 2, 0);
  if (fd < 0) {
    printf("netbench: udp socket failed\n");
    return;
  }
  if (bind(fd, PORT) < 0) {
    printf("netbench: udp bind failed\n");
    return;
  }
  memset(msg, 'u', sizeof(msg));

  int n = 200;
  uint64 t0 = rdtime();
  for (int i = 0; i < n; i++) {
    int ip, port;
    sendto(fd, LOCAL_IP, PORT, msg, sizeof(msg));
    recvfrom(fd, buf, sizeof(buf), &ip, &port);
  }
  uint64 t1 = rdtime();
  printf("netbench: udp %d rtt, %ld us total, %ld us/rtt\n", n, (t1 - t0) / 10,
         (t1 - t0) / 10 / n);
  close(fd);
}

static void
bench_tcp(void)
{
  int ls = socket(2, 1, 0);
  if (ls < 0) {
    printf("netbench: tcp socket failed\n");
    return;
  }
  if (bind(ls, PORT + 1) < 0 || listen(ls) < 0) {
    printf("netbench: tcp listen failed\n");
    return;
  }

  int pid = fork();
  if (pid == 0) {
    int c = socket(2, 1, 0);
    if (c < 0 || connect(c, LOCAL_IP, PORT + 1) < 0) {
      printf("netbench: connect failed\n");
      exit(1);
    }
    printf("netbench: child connected\n");
    memset(buf, 't', sizeof(buf));
    for (int i = 0; i < 16; i++)
      write(c, buf, sizeof(buf));
    close(c);
    exit(0);
  }

  int fd = accept(ls);
  printf("netbench: parent accepted fd=%d\n", fd);
  if (fd < 0) {
    printf("netbench: tcp accept failed\n");
    return;
  }
  int total = 0, n;
  uint64 t0 = rdtime();
  while ((n = read(fd, buf, sizeof(buf))) > 0)
    total += n;
  uint64 t1 = rdtime();
  uint64 us = (t1 - t0) / 10;
  printf("netbench: tcp %d bytes, %ld us, %ld KB/s\n", total, us,
         us ? (uint64)total * 1000 / us : 0);
  close(fd);
  close(ls);
  wait(0);
}

int
main(void)
{
  bench_udp();
  bench_tcp();
  exit(0);
}
