# 外部函数声明 in trap_kernel.c
.globl trap_kernel_handler


# S-mode 中断处理 (包括软件中断和外设中断)
.globl kernel_vector
.align 4
kernel_vector:

        # ========== 上下文保存阶段 ==========
        
        # 1. 分配栈空间 (288字节的trapframe)
        addi sp, sp, -288
        
        # 2. 保存所有32个通用寄存器 (x0-x31)
        sd x0,  0(sp)    # zero - 为了结构一致性
        sd x1,  8(sp)    # ra - 返回地址
        sd x2,  16(sp)   # sp - 暂存，稍后修正
        sd x3,  24(sp)   # gp - 全局指针
        sd x4,  32(sp)   # tp - 线程指针
        sd x5,  40(sp)   # t0 - 临时寄存器
        sd x6,  48(sp)   # t1
        sd x7,  56(sp)   # t2  
        sd x8,  64(sp)   # s0/fp - 帧指针
        sd x9,  72(sp)   # s1
        sd x10, 80(sp)   # a0 - 参数/返回值
        sd x11, 88(sp)   # a1
        sd x12, 96(sp)   # a2
        sd x13, 104(sp)  # a3
        sd x14, 112(sp)  # a4
        sd x15, 120(sp)  # a5
        sd x16, 128(sp)  # a6
        sd x17, 136(sp)  # a7
        sd x18, 144(sp)  # s2
        sd x19, 152(sp)  # s3
        sd x20, 160(sp)  # s4
        sd x21, 168(sp)  # s5
        sd x22, 176(sp)  # s6
        sd x23, 184(sp)  # s7
        sd x24, 192(sp)  # s8
        sd x25, 200(sp)  # s9
        sd x26, 208(sp)  # s10
        sd x27, 216(sp)  # s11
        sd x28, 224(sp)  # t3
        sd x29, 232(sp)  # t4
        sd x30, 240(sp)  # t5
        sd x31, 248(sp)  # t6
        
        # 3. 修正sp的保存值（因为已经调整了sp）
        addi t0, sp, 288
        sd t0, 16(sp)    # 保存原始sp值
        
        # 4. 保存CSR寄存器
        csrr t0, sepc
        sd t0, 256(sp)   # 保存异常PC
        
        csrr t0, sstatus  
        sd t0, 264(sp)   # 保存状态寄存器
        
        csrr t0, scause
        sd t0, 272(sp)   # 保存异常原因
        
        csrr t0, stval
        sd t0, 280(sp)   # 保存异常值
        
        # ========== 调用C处理函数 ==========
        
        # 5. 设置参数并调用C函数
        mv a0, sp        # 将trapframe指针作为参数
        call kerneltrap  # 调用C中断处理函数
        
        # ========== 上下文恢复阶段 ==========
        
        # 6. 恢复CSR寄存器
        ld t0, 256(sp)   # 恢复sepc
        csrw sepc, t0
        
        ld t0, 264(sp)   # 恢复sstatus
        csrw sstatus, t0
        
        # scause和stval是只读的，不需要恢复
        
        # 7. 恢复所有32个通用寄存器
        ld x1,  8(sp)    # ra
        ld x3,  24(sp)   # gp
        ld x4,  32(sp)   # tp
        ld x5,  40(sp)   # t0
        ld x6,  48(sp)   # t1
        ld x7,  56(sp)   # t2
        ld x8,  64(sp)   # s0/fp
        ld x9,  72(sp)   # s1
        ld x10, 80(sp)   # a0
        ld x11, 88(sp)   # a1
        ld x12, 96(sp)   # a2
        ld x13, 104(sp)  # a3
        ld x14, 112(sp)  # a4
        ld x15, 120(sp)  # a5
        ld x16, 128(sp)  # a6
        ld x17, 136(sp)  # a7
        ld x18, 144(sp)  # s2
        ld x19, 152(sp)  # s3
        ld x20, 160(sp)  # s4
        ld x21, 168(sp)  # s5
        ld x22, 176(sp)  # s6
        ld x23, 184(sp)  # s7
        ld x24, 192(sp)  # s8
        ld x25, 200(sp)  # s9
        ld x26, 208(sp)  # s10
        ld x27, 216(sp)  # s11
        ld x28, 224(sp)  # t3
        ld x29, 232(sp)  # t4
        ld x30, 240(sp)  # t5
        ld x31, 248(sp)  # t6
        
        # 8. 最后恢复sp（释放栈空间）
        ld x2, 16(sp)    # 恢复原始sp值
        
        # 9. 返回被中断的程序
        sret             # 返回，自动从sepc恢复PC


# M-mode 中断处理(只包括时钟中断)
.globl timer_vector
.align 4
timer_vector:

        # 暂存寄存器 a0 a1 a2 a3
        # 将MSR寄存器 mscratch 放入 a0
        csrrw a0, mscratch, a0
        sd a1, 0(a0)      # mscratch[0] = a1
        sd a2, 8(a0)      # mscratch[1] = a2
        sd a3, 16(a0)     # mscratch[2] = a3

        # CLINT_MTIMECMP(hartid) = CLINT_MTIMECMP(hartid) + INTERVAL
        # 以便响应下一次时钟中断
        ld a1, 24(a0)     # a1 = mscratch[3] 里面放了 CLINT_MTIMECMP(hartid)
        ld a2, 32(a0)     # a2 = mscratch[4] 里面放了 INTERVAL        
        ld a3, 0(a1)
        add a3, a3, a2
        sd a3, 0(a1)

        # 引发一个 S-mode software interrupt
        li a1, 2
        csrw sip, a1

        # 恢复寄存器 a0 a1 a2 a3
        # 将 mscratch 寄存器恢复
        ld a3, 16(a0)
        ld a2, 8(a0)
        ld a1, 0(a0)
        csrrw a0, mscratch, a0

        mret
