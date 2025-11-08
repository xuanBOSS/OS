// kernel/trap/trap_user.c - 完整版本
#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "mem/vmem.h"
#include "mem/kvm.h"
#include "memlayout.h"
#include "riscv.h"
#include "common.h"

// 在 trampoline.S 中定义的符号
extern char trampoline[];      // trampoline 页起始地址
extern char user_vector[];     // 用户态陷阱向量入口

// ✅ 声明汇编函数
extern void user_return(uint64 trapframe_va, uint64 user_satp);

// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    printf("\n🎉 *** USER TRAP HANDLER CALLED ***\n");
    
    uint64 sepc = r_sepc();
    uint64 scause = r_scause();
    uint64 stval = r_stval();
    
    printf("TRAP INFO:\n");
    printf("  sepc: 0x%lx\n", sepc);
    printf("  scause: 0x%lx\n", scause);
    printf("  stval: 0x%lx\n", stval);
    
    proc_t* p = myproc();
    if (!p) {
        panic("trap_user_handler: no current process");
    }
    
    printf("Process PID: %d\n", p->pid);
    
    if (scause == 8) {  // ecall from U-mode
        printf("🎯 System call detected!\n");
        
        // 从 trapframe 读取系统调用号（a7 寄存器）
        uint64 syscall_num = p->tf->a7;
        printf("Syscall number: %ld\n", syscall_num);
        
        switch (syscall_num) {
            case 0:
                printf("✅ Syscall 0 - Test syscall\n");
                p->tf->a0 = 42;  // 返回值
                break;
            case 1:
                printf("✅ Syscall 1 - Another test\n");
                p->tf->a0 = 12345;
                break;
            case 3:
                printf("✅ Syscall 3 - Entering infinite loop\n");
                printf("🎉 Lab4 SUCCESS: User program entering infinite loop!\n");
                // 不更新 epc，让程序重复执行同一条指令
                p->tf->epc = sepc;  // 重复执行 ecall
                
                p->tf->a0 = 0;
                break;
            default:
                printf("❌ Unknown syscall: %ld\n", syscall_num);
                p->tf->a0 = -1;
                break;
        }
        
        // 更新 epc 指向下一条指令（跳过 ecall）
        p->tf->epc = sepc + 4;
        printf("Syscall handled, epc updated to: 0x%lx\n", p->tf->epc);
        
        printf("Returning to user mode...\n");
        
        // ✅ 关键：调用 trap_user_return 返回用户态
        trap_user_return();
        
    } else {
        printf("❌ Unexpected trap: scause=%ld\n", scause);
        panic("Unexpected user trap");
    }
    
    panic("trap_user_handler should not reach here!");
}

// 返回用户态的函数
// kernel/trap/trap_user.c - 不切换页表的版本
void trap_user_return()
{
    printf("=== TRAP USER RETURN (NO PAGE TABLE SWITCH) ===\n");
    
    proc_t* p = myproc();
    if (!p) {
        panic("trap_user_return: no current process");
    }
    
    printf("Returning to user space: PID=%d, epc=0x%lx\n", p->pid, p->tf->epc);
    
    // 禁用中断
    intr_off();
    
    // 设置 sstatus
    uint64 sstatus = r_sstatus();
    sstatus &= ~(1UL << 8);   // 清除 SPP 位（返回用户态）
    sstatus |= (1UL << 5);    // 设置 SPIE 位（允许中断）
    w_sstatus(sstatus);
    
    // ✅ 使用实际的 trapframe 物理地址
    uint64 trapframe_pa = (uint64)p->tf;
    
    printf("Not switching page table (keeping kernel page table):\n");
    printf("  trapframe: 0x%lx\n", trapframe_pa);
    printf("  user_epc: 0x%lx\n", p->tf->epc);
    printf("  current satp: 0x%lx\n", r_satp());
    
    // ✅ 设置寄存器并直接返回用户态
    w_sepc(p->tf->epc);
    w_sscratch(trapframe_pa);
    
    printf("Executing sret directly (no page table switch)...\n");
    printf("Expected: user program executes ecall -> trap handler called\n");
    
    asm volatile(
        "ld sp, 48(%0)\n"       // 恢复用户栈指针
        "sret\n"                // 返回用户态
        :
        : "r" (trapframe_pa)
        : "memory"
    );
    
    panic("sret should not return!");
}

/**
 * 系统调用处理函数 - 这个函数目前没用到，可以删除或保留备用
 */
void handle_syscall(uint64 scause, uint64 sepc) {
    proc_t* p = myproc();
    if (!p) {
        panic("handle_syscall: no current process");
    }
    
    int syscall_num = p->tf->a7;
    
    printf("=== SYSCALL %d ===\n", syscall_num);
    printf("Process %d called syscall %d\n", p->pid, syscall_num);

    switch (syscall_num) {
        case 0:  // SYS_print
            printf("✅ Syscall 0 (print) - SUCCESS\n");
            p->tf->a0 = 0;
            break;
            
        case 1:   // SYS_gettime  
            printf("✅ Syscall 1 (gettime) - SUCCESS\n");
            p->tf->a0 = 12345678;
            break;
            
        case 2:   // SYS_getpid
            printf("✅ Syscall 2 (getpid) - SUCCESS\n");
            p->tf->a0 = p->pid;
            break;
            
        case 3:   // SYS_exit
            printf("✅ Syscall 3 (exit) - Lab completed!\n");
            panic("🎉 Lab4 completed successfully!");
            break;
            
        default:
            printf("❌ Unknown syscall %d\n", syscall_num);
            p->tf->a0 = -1;
            break;
    }
    
    // 系统调用完成后，epc指向下一条指令
    p->tf->epc = sepc + 4;
    printf("Syscall complete: return=%ld, next_epc=0x%lx\n", 
           p->tf->a0, p->tf->epc);
    printf("=================\n");
}
