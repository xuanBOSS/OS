# kernel/trap/kernelvec.S
# RISC-V 中断和异常入口点

.globl kernelvec
.globl timervec
.align 4

# 内核中断/异常入口
kernelvec:
    # 分配栈帧空间 (512字节，确保16字节对齐)
    addi sp, sp, -512

    # 保存所有32个通用寄存器
    # 注意：x2(sp)保存的是原始栈指针值
    sd x0, 0(sp)      # zero
    sd x1, 8(sp)      # ra (返回地址)
    addi t0, sp, 512  # 计算原始sp值
    sd t0, 16(sp)     # sp (原始值)
    sd x3, 24(sp)     # gp
    sd x4, 32(sp)     # tp
    sd x5, 40(sp)     # t0
    sd x6, 48(sp)     # t1
    sd x7, 56(sp)     # t2
    sd x8, 64(sp)     # s0/fp
    sd x9, 72(sp)     # s1
    sd x10, 80(sp)    # a0
    sd x11, 88(sp)    # a1
    sd x12, 96(sp)    # a2
    sd x13, 104(sp)   # a3
    sd x14, 112(sp)   # a4
    sd x15, 120(sp)   # a5
    sd x16, 128(sp)   # a6
    sd x17, 136(sp)   # a7
    sd x18, 144(sp)   # s2
    sd x19, 152(sp)   # s3
    sd x20, 160(sp)   # s4
    sd x21, 168(sp)   # s5
    sd x22, 176(sp)   # s6
    sd x23, 184(sp)   # s7
    sd x24, 192(sp)   # s8
    sd x25, 200(sp)   # s9
    sd x26, 208(sp)   # s10
    sd x27, 216(sp)   # s11
    sd x28, 224(sp)   # t3
    sd x29, 232(sp)   # t4
    sd x30, 240(sp)   # t5
    sd x31, 248(sp)   # t6

    # 保存CSR寄存器到偏移256开始的位置
    csrr t0, sepc
    sd t0, 256(sp)    # sepc
    csrr t0, sstatus
    sd t0, 264(sp)    # sstatus
    csrr t0, scause
    sd t0, 272(sp)    # scause
    csrr t0, stval
    sd t0, 280(sp)    # stval

    # 设置trapframe指针作为参数
    mv a0, sp         # 将栈指针(trapframe地址)作为参数传递

    # 调用C语言中断处理函数
    call kerneltrap

    # 恢复CSR寄存器
    ld t0, 256(sp)
    csrw sepc, t0
    ld t0, 264(sp)
    csrw sstatus, t0
    # 注意：scause和stval在中断处理过程中可能被硬件修改，不需要恢复

    # 恢复所有32个通用寄存器
    ld x1, 8(sp)      # ra
    ld x3, 24(sp)     # gp
    ld x4, 32(sp)     # tp
    ld x5, 40(sp)     # t0
    ld x6, 48(sp)     # t1
    ld x7, 56(sp)     # t2
    ld x8, 64(sp)     # s0/fp
    ld x9, 72(sp)     # s1
    ld x10, 80(sp)    # a0
    ld x11, 88(sp)    # a1
    ld x12, 96(sp)    # a2
    ld x13, 104(sp)   # a3
    ld x14, 112(sp)   # a4
    ld x15, 120(sp)   # a5
    ld x16, 128(sp)   # a6
    ld x17, 136(sp)   # a7
    ld x18, 144(sp)   # s2
    ld x19, 152(sp)   # s3
    ld x20, 160(sp)   # s4
    ld x21, 168(sp)   # s5
    ld x22, 176(sp)   # s6
    ld x23, 184(sp)   # s7
    ld x24, 192(sp)   # s8
    ld x25, 200(sp)   # s9
    ld x26, 208(sp)   # s10
    ld x27, 216(sp)   # s11
    ld x28, 224(sp)   # t3
    ld x29, 232(sp)   # t4
    ld x30, 240(sp)   # t5
    ld x31, 248(sp)   # t6

    # 最后恢复栈指针和x0
    ld x0, 0(sp)      # zero (虽然不起作用，但保持一致性)
    addi sp, sp, 512  # 释放栈帧

    # 返回到中断点
    sret

# 机器模式定时器中断向量 (为未来Task 5准备)
timervec:
    # 简单的机器模式定时器处理
    # 设置S模式软件中断，然后返回S模式处理
    
    # 保存一个寄存器用于工作
    csrrw a0, mscratch, a0
    
    # 触发S模式软件中断
    li a1, 2
    csrw sip, a1
    
    # 重新设置下一次定时器中断
    # TODO: 在Task 5中实现具体的定时器重新设置
    
    # 恢复寄存器并返回
    csrrw a0, mscratch, a0
    mret
