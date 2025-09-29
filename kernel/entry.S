# kernel/entry.S
.section .text
.global _start

_start:
    # 获取当前CPU ID（在RISC-V中通过mhartid CSR）
    csrr t0, mhartid
    bnez t0, park_other_cores    # 如果不是CPU 0，跳转到park

    # 调试：输出启动标记 'S'
    li t0, 0x10000000    
    li t1, 'S'           
    sb t1, 0(t0)         

    # 设置栈指针
    la sp, stack_end     
    
    # 调试：输出栈设置完成标记 'P' 
    li t1, 'P'           
    sb t1, 0(t0)

    # 清零 BSS 段
    call clear_bss

    # 调试：输出BSS清理完成标记 'B'
    li t0, 0x10000000    # 重新加载UART基地址
    li t1, 'B'
    sb t1, 0(t0)

    # 跳转到 C 主函数
    call main

    # 死循环
spin:
    j spin

# 其他CPU核心停放区
park_other_cores:
    # 让其他核心进入无限等待
    wfi                  # 等待中断（节能）
    j park_other_cores

# BSS 清零函数
clear_bss:
    la t0, bss_start    
    la t1, bss_end      
    li t2, 0            
    
    bge t0, t1, bss_done

clear_loop:
    sb t2, 0(t0)        
    addi t0, t0, 1      
    blt t0, t1, clear_loop

bss_done:
    ret
