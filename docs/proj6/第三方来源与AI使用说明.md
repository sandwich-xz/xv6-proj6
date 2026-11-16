# 第三方来源与 AI 使用说明

赛题要求：功能赛道的源代码与设计方案文档中，必须明确说明所复制的非本队作品内容来源、
与它的区别、以及本队的创新点。

## 一、基线代码

| 来源 | 用途 | 许可 |
| --- | --- | --- |
| MIT xv6-riscv rev5 | 教学内核原始版本 | MIT |
| 2026 北航扩展版（颜值爆表队，proj34） | 本项目的直接基线：VFS + HAL + 三文件系统 + 双架构 | BSD-2-Clause（随仓库 LICENSE） |

本项目**全部改动**均以该基线的 `main` 分支为参照，交付时提供 `git diff` 生成的补丁。

## 二、参考资料

| 资料 | 用途 |
| --- | --- |
| virtio 1.1 规范 | virtio-net 设备初始化、virtqueue、virtio-pci capability |
| xv6-riscv-book | 内核结构、页表与陷入处理 |
| RISC-V ELF psABI | 重定位类型（RELATIVE / JUMP_SLOT / 64） |
| Linux `ld.so` 行为 | 动态链接器流程对照（仅作参考，未复制代码） |

## 三、AI 工具使用

开发过程中使用 AI 编程助手完成部分代码草稿与文档，具体约定：

1. 所有 AI 生成的代码均经过人工逐行审阅，并在 QEMU 中实测（见测试记录）。
2. AI 不参与"设计决策"的最终判断：netdev 抽象、传输层分叉、diff 预算等由队员决定。
3. 生成的重定位、协议栈代码若与规范不符，以规范为准并记录修正过程。
4. 本文件与最终报告中的"问题与解决"章节，会如实记录 AI 建议被否决或修正的例子。

## 四、本队原创部分

- `kernel/netdev.*`、`kernel/ether.c`、`arp.c`、`ip.c`、`icmp.c`、`udp.c`、`tcp.c`、`socket.c`、`sysnet.c`
- `kernel/virtio_net.c`（两个架构共用队列逻辑，传输层分叉）
- `kernel/arch/loongarch/virtio_net_pci.c`（LoongArch 传输层，待完成）
- `kernel/exec.c` 的 `PT_INTERP`/auxv 支持、`kernel/sysproc.c` 的 `mmap`
- `user/ldstub.c`（动态链接器）、`user/libmath.c`、四个自测程序
