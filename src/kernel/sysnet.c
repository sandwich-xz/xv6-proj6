// SPDX-License-Identifier: BSD-2-Clause
//
// 网络系统调用：socket / bind / sendto / recvfrom。
// 参数刻意用整数，不引入 sockaddr 结构，保持 xv6 的简单风格。
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

uint64
sys_socket(void)
{
  int domain, type, proto;
  struct file *f;
  struct sock *s;
  int fd;

  argint(0, &domain);
  argint(1, &type);
  argint(2, &proto);
  (void)domain;
  (void)type;
  (void)proto;

  s = sock_alloc(type == 1 ? 1 : 0);
  if (s == 0)
    return -1;
  f = filealloc();
  if (f == 0) {
    sock_free(s);
    return -1;
  }
  f->type = FD_SOCK;
  f->sock = s;
  f->readable = 1;
  f->writable = 1;
  if ((fd = fdalloc(f)) < 0) {
    fileclose(f);
    return -1;
  }
  return fd;
}

uint64
sys_bind(void)
{
  int fd, port;
  struct file *f;

  argint(0, &fd);
  argint(1, &port);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (f->type != FD_SOCK)
    return -1;
  return sock_bind(f->sock, (ushort)port);
}

uint64
sys_sendto(void)
{
  int fd, ip, port, len;
  uint64 buf;
  struct file *f;
  static uchar kbuf[ETH_MTU];

  argint(0, &fd);
  argint(1, &ip);
  argint(2, &port);
  argaddr(3, &buf);
  argint(4, &len);

  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (f->type != FD_SOCK)
    return -1;
  if (len < 0 || len > ETH_MTU - 28)
    return -1;
  if (copyin(myproc()->pagetable, (char *)kbuf, buf, len) < 0)
    return -1;

  return udp_send(sock_port(f->sock), (uint)ip, (ushort)port, kbuf, len);
}

uint64
sys_recvfrom(void){
  int fd, len;
  uint64 buf, ipaddr, portaddr;
  struct file *f;
  uint srcip = 0;
  ushort sport = 0;
  int n;
  static uchar kbuf[ETH_MTU];

  argint(0, &fd);
  argaddr(1, &buf);
  argint(2, &len);
  argaddr(3, &ipaddr);
  argaddr(4, &portaddr);

  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (f->type != FD_SOCK)
    return -1;
  if (len <= 0 || len > ETH_MTU)
    return -1;

  n = sock_recv(f->sock, kbuf, len, &srcip, &sport);
  if (n < 0)
    return -1;
  if (copyout(myproc()->pagetable, buf, (char *)kbuf, n) < 0)
    return -1;
  if (ipaddr && copyout(myproc()->pagetable, ipaddr, (char *)&srcip, sizeof(srcip)) < 0)
    return -1;
  if (portaddr && copyout(myproc()->pagetable, portaddr, (char *)&sport, sizeof(sport)) < 0)
    return -1;
  return n;
}

uint64
sys_connect(void)
{
  int fd, ip, port;
  struct file *f;

  argint(0, &fd);
  argint(1, &ip);
  argint(2, &port);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (f->type != FD_SOCK)
    return -1;
  return sock_connect(f->sock, (uint)ip, (ushort)port);
}

uint64
sys_listen(void)
{
  int fd;
  struct file *f;

  argint(0, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (f->type != FD_SOCK)
    return -1;
  return sock_listen(f->sock);
}

uint64
sys_accept(void)
{
  int fd, nfd;
  struct file *f, *nf;
  struct sock *ns;

  argint(0, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (f->type != FD_SOCK)
    return -1;

  ns = sock_accept(f->sock);
  if (ns == 0)
    return -1;
  nf = filealloc();
  if (nf == 0) {
    sock_free(ns);
    return -1;
  }
  nf->type = FD_SOCK;
  nf->sock = ns;
  nf->readable = 1;
  nf->writable = 1;
  if ((nfd = fdalloc(nf)) < 0) {
    fileclose(nf);
    return -1;
  }
  return nfd;
}
