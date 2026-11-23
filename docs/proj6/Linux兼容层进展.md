# Linux 兼容层进展与结论（2026-11-23）

> 目标：让 2026 国赛的官方测例（busybox + `xxxxx_testcode.sh`）能在本内核上运行。
> 现状：**核心链路已通，busybox 的 ash 执行脚本这一步未完成。**

## 1. 已经能跑的证据

| 程序 | 结果 | 命令 |
| --- | --- | --- |
| musl 静态 hello（8.9 KB） | 输出 `static-hello` | `/mnt/hello` |
| busybox 版本信息 | 正常打印 banner 与 usage | `/mnt/busybox` |
| busybox echo / uname | `hello-busybox` / `Linux GNU/Linux` | `/mnt/busybox echo ...` |
| busybox cat | 完整读出 158 字节脚本内容 | `/mnt/busybox cat /mnt/basic_testcode.sh` |
| busybox sh 启动 | 能进入 ash，能打开脚本 | `lxrun`（自写 execve 启动器） |

## 2. 修掉的根因（按价值排序）

1. **RISC-V 没开 FPU**：`sstatus.FS` 未置位，`fsd` 触发非法指令（`scause=0x2`）。musl 与 glibc 的 `setjmp` 都保存浮点寄存器，所以两者崩在同一段代码、同一个"坏指针"。修复：`usertrapret` 里 `x |= SSTATUS_FS`。
2. **`loadseg` 假设页对齐**：Linux 允许 `p_vaddr` 非页对齐（与 `p_offset` 同余），原实现把数据写到页首，程序读到全零。修复：按页内偏移逐段拷贝。
3. **栈上 argv/argc/envp/auxv 布局**：Linux 的 `sp` 指向 argc 字、argv 紧随其后，且 envp 与 auxv 必须紧邻（中间不能有对齐填充）。修复后 glibc 能跑过 `_dl_aux_init`。
4. **`is_linux` 分流**：Linux 的 `dup(23)`/`fcntl(25)` 与 xv6 号段重叠，必须按进程 ABI 整表分流。**注意**：该字段在 `kfork` 里漏复制，导致子进程退回 xv6 ABI——这是"看起来卡死"的主要原因。
5. **`fstat` 语义**：原来一律返回字符设备，ash 判定脚本"不是普通文件"直接退出。修复：按 inode 的 `type`/`size` 填 Linux `struct stat`。

## 3. 当前未完成的一步

`busybox sh t1.sh`（脚本内容只有一行 `echo builtin-ok`）：ash 被成功 exec（`kexec` 返回 argc=2）、能打开脚本，但随后**不输出、不再发系统调用**。

已排除：镜像内容、argv 传递、execve 是否被调用、文件读写、输出走 writev、wait4 缺失。

下一步建议：打印 ash 的全部系统调用（含 `exit_group`）与 `sleep` 等待通道，确认它是"睡在某个等待上"还是"解析阶段死循环"。

## 4. 已实现的 Linux 系统调用子集

`write/read/writev/openat/close/lseek/exit/exit_group/brk/mmap/munmap/mprotect/set_tid_address/set_robust_list/rseq/rt_sigaction/rt_sigprocmask/prctl/uname/getpid/getppid/getuid/geteuid/getgid/getegid/clock_gettime/gettimeofday/getrandom/ioctl/fstat/newfstatat/readlinkat/prlimit64/dup/dup3/fcntl/clone/execve/chdir/getcwd/setpgid/getpgid/wait4`

## 5. 复现方式

```bash
# 内核：make ARCH=riscv ...
# 工具链：musl 从源码交叉编译（见 scripts 记录），或直接用 /opt/musl
# 镜像：busybox 与脚本写入 ext2 分区（xv6fs 单文件上限 274 KB，放不下 1.15 MB 的 busybox）
```
