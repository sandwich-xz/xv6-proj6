# Linux ABI 最小程序：不依赖 libc，直接 ecall。
# 用来隔离"内核 ABI 是否正确"与"glibc 启动假设是否满足"。
    .section .text
    .globl _start
_start:
    li   a0, 1              # fd = 1
    la   a1, msg            # buf
    li   a2, 13             # len
    li   a7, 64             # Linux write
    ecall

    # 再写一次，确认不是一次性偶然
    li   a0, 1
    la   a1, msg2
    li   a2, 12
    li   a7, 64
    ecall

    li   a0, 0
    li   a7, 93             # Linux exit
    ecall

    .section .rodata
msg:
    .asciz "asm-hello-lx\n"
msg2:
    .asciz "asm-second\n"
