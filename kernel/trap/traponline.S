# kernel/trap/trampoline.S
# 这部分代码在用户内核切换时发挥作用
# 不论是内核页表还是用户页表, 这部分代码被映射在同一个虚拟地址空间

# kernel/trap/trampoline.S
.section trampsec

.globl trampoline
trampoline:
    nop
    nop
    nop
    nop

.align 4
.globl user_vector
user_vector:

#------------------保存用户寄存器 (begin)-----------------------

    # sscratch 存放了 p->trapframe
    csrrw a0, sscratch, a0

    csrr t0, sepc
    sd t0, 24(a0)

    # 保存通用寄存器到 trapframe
    sd ra, 40(a0)
    sd sp, 48(a0)
    sd gp, 56(a0)
    sd tp, 64(a0)      # 保存用户 tp
    sd t0, 72(a0)
    sd t1, 80(a0)
    sd t2, 88(a0)
    sd s0, 96(a0)
    sd s1, 104(a0)
    sd a1, 120(a0)
    sd a2, 128(a0)
    sd a3, 136(a0)
    sd a4, 144(a0)
    sd a5, 152(a0)
    sd a6, 160(a0)
    sd a7, 168(a0)
    sd s2, 176(a0)
    sd s3, 184(a0)
    sd s4, 192(a0)
    sd s5, 200(a0)
    sd s6, 208(a0)
    sd s7, 216(a0)
    sd s8, 224(a0)
    sd s9, 232(a0)
    sd s10, 240(a0)
    sd s11, 248(a0)
    sd t3, 256(a0)
    sd t4, 264(a0)
    sd t5, 272(a0)
    sd t6, 280(a0)

    # 保存用户 a0 到 trapframe
    csrr t0, sscratch
    sd t0, 112(a0)

#------------------保存用户寄存器 (end)-------------------------

    # ✅ 加载内核上下文（放在保存之后，避免覆盖）
    ld sp, 8(a0)       # sp = tf->kernel_sp
    ld tp, 32(a0)      # tp = tf->kernel_hartid ← 这样才不会被之前的 sd 覆盖！
    ld t0, 16(a0)      # t0 = tf->kernel_trap

    # 切换到内核页表
    ld t1, 0(a0)       # t1 = tf->kernel_satp
    csrw satp, t1
    sfence.vma zero, zero

    # 跳转到 trap_user_handler()
    jr t0

# 用户态 trap 处理完成后返回
.globl user_return
user_return:

    # 切换到用户页表
    csrw satp, a1
    sfence.vma zero, zero

    ld t0, 24(a0)
    csrw sepc, t0

#---------------------恢复用户寄存器 (begin)----------------------
    ld t0, 112(a0)
    csrw sscratch, t0 

    ld ra, 40(a0)
    ld sp, 48(a0)
    ld gp, 56(a0)
    ld tp, 64(a0)
    ld t0, 72(a0)
    ld t1, 80(a0)
    ld t2, 88(a0)
    ld s0, 96(a0)
    ld s1, 104(a0)
    ld a1, 120(a0)
    ld a2, 128(a0)
    ld a3, 136(a0)
    ld a4, 144(a0)
    ld a5, 152(a0)
    ld a6, 160(a0)
    ld a7, 168(a0)
    ld s2, 176(a0)
    ld s3, 184(a0)
    ld s4, 192(a0)
    ld s5, 200(a0)
    ld s6, 208(a0)
    ld s7, 216(a0)
    ld s8, 224(a0)
    ld s9, 232(a0)
    ld s10, 240(a0)
    ld s11, 248(a0)
    ld t3, 256(a0)
    ld t4, 264(a0)
    ld t5, 272(a0)
    ld t6, 280(a0)

    csrrw a0, sscratch, a0

#---------------------恢复用户寄存器 (end)----------------------

    sret
