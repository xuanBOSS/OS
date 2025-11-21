.section .text
.global _entry
.global simple_trap
.global smode_test

_entry:
    # 获取当前CPU ID
    csrr t0, mhartid

    # 如果不是CPU 0，直接进入等待循环
    bnez t0, spin

    # 设置栈指针: CPU_stack + 4096 * (hartid + 1)
    la sp, CPU_stack
    li a0, 4096
    csrr a1, mhartid
    addi a1, a1, 1
    mul a0, a0, a1
    add sp, sp, a0

    # 🔥 重要：调用 start() 进行 M-mode 初始化
    call start
    
    # 注意：start() 函数会执行 mret，如果执行到这里说明出错了
    li t0, 0x10000000
    li t1, 'E'
    sb t1, 0(t0)
    li t1, 'R'
    sb t1, 0(t0)
    li t1, 'R'
    sb t1, 0(t0)
    li t1, '\n'
    sb t1, 0(t0)

# 其他CPU核心等待
spin:
    wfi
    j spin

# 简单异常打印，打印 "TRAP!\n"
simple_trap:
    li t0, 0x10000000  # UART基地址

    li t1, 'T'
    sb t1, 0(t0)
    li t1, 'R'
    sb t1, 0(t0)
    li t1, 'A'
    sb t1, 0(t0)
    li t1, 'P'
    sb t1, 0(t0)
    li t1, '!'
    sb t1, 0(t0)
    li t1, '\n'
    sb t1, 0(t0)

trap_loop:
    wfi
    j trap_loop

# S-mode测试代码，打印 "SMODE!\n" 并循环
smode_test:
    # 打印成功信息
    li t0, 0x10000000
    li t1, 'S'
    sb t1, 0(t0)
    li t1, '-'
    sb t1, 0(t0)
    li t1, 'O'
    sb t1, 0(t0)
    li t1, 'K'
    sb t1, 0(t0)
    li t1, '\n'
    sb t1, 0(t0)

    # 检查栈指针 - 确保栈是正确的
    # 当前栈应该还是 M-mode 设置的栈
    
    # 跳转到 main 函数继续执行
    call main

    # 如果 main 返回，进入无限循环
smode_loop:
    wfi
    j smode_loop

.section .bss
.align 12
.global CPU_stack
CPU_stack:
    .zero 12288       # 12 KB 栈，支持3核，每核4KB
    