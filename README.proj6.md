# proj6：xv6 网络与动态链接（吉人自有天相）

2026 全国大学生计算机系统能力大赛·操作系统设计赛·中西部与华南区域赛·功能挑战赛道
赛题编号：**proj6**；赛题名称：**xv6 网络和动态链接功能的设计与实现**

- 学校：国防科技大学
- 队伍：吉人自有天相
- 队员：谢喆（队长）、陈泓宇、王雨然
- 指导教师：文艳军

## 这是什么

在 2026 北航扩展版 xv6（已含 VFS 与 HAL，支持 xv6fs/ext2/fat32、RISC-V/LoongArch）之上，
增加两件事：

1. **网络**：virtio-net 驱动（RISC-V 用 virtio-mmio）、ARP / IPv4 / ICMP / UDP / TCP、
   socket 系统调用（socket/bind/sendto/recvfrom/connect/listen/accept），支持环回。
2. **动态链接**：`mmap` 系统调用、`exec` 的 `PT_INTERP` 与 auxv、用户态 `ld.so`
   （按 `DT_NEEDED` 加载共享库、RELATIVE 重定位、JUMP_SLOT 符号解析）。

## 快速上手

```sh
cd src
make ARCH=riscv TOOLPREFIX=riscv64-linux-gnu- kernel/kernel fs.img
make ARCH=riscv TOOLPREFIX=riscv64-linux-gnu- qemu
```

启动后可直接运行以下自测程序：

| 命令 | 验证内容 |
| --- | --- |
| `dltest` | 用户态加载 libmath.so，调用 add/mul/fib |
| `dynhello` | PT_INTERP + ld.so 动态链接，调用库函数 |
| `udptest` | UDP socket 环回收发 |
| `tcptest` | TCP 三次握手 + 双向收发 |

开机自检会自动完成：ARP 请求 → 收到网关应答 → ping 网关（打印 RTT）。

## 目录

| 路径 | 说明 |
| --- | --- |
| `src/kernel/netdev.*` | 网卡抽象（对齐 `blockdev`） |
| `src/kernel/virtio_net.c` | virtio-net 驱动（mmio / pci 共用队列逻辑） |
| `src/kernel/{arp,ip,icmp,udp,tcp}.c` | 协议栈 |
| `src/kernel/socket.c`、`sysnet.c` | socket 层与系统调用 |
| `src/kernel/exec.c`、`sysproc.c` | PT_INTERP / auxv / mmap |
| `src/user/ldstub.c` | 用户态动态链接器（构建为 `/ld.so`） |
| `src/user/{dltest,dynhello,udptest,tcptest}.c` | 自测程序 |

## 当前状态（如实）

| 项 | 状态 |
| --- | --- |
| RISC-V 网络 | 通过（ARP/ICMP/UDP/TCP 自测） |
| 动态链接 | 通过（PT_INTERP + .so + JUMP_SLOT） |
| usertests 回归 | 通过（`ALL TESTS PASSED`） |
| LoongArch 启动 | 通过（磁盘读较慢：完成中断未接，靠看门狗） |
| LoongArch 网卡 | 通过（virtio-pci，ARP/ICMP/UDP/TCP） |

## 合规

- 许可证：沿用基线的 BSD-2-Clause（见 `src/LICENSE`）
- 参考与第三方来源：xv6-riscv rev5、2026 北航扩展版、virtio 规范
- AI 辅助：开发过程中使用 AI 工具生成部分代码与文档，均已人工审阅并在测试中验证
