// kernel/trap/trap_user.c - 修复系统调用处理
#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "mem/vmem.h"
#include "mem/kvm.h"
#include "memlayout.h"
#include "riscv.h"
#include "common.h"
#include "syscall/syscall.h"
#include "syscall/syscall_table.h"

// 在 trampoline.S 中定义的符号
extern char trampoline[];
extern char user_vector[];

extern syscall_desc_t syscall_table[];
extern const int syscall_table_size;

void handle_user_trap_error(uint64 scause, uint64 sepc, uint64 stval) {
    proc_t *p = myproc();
    
    printf("❌ User trap error details:\n");
    printf("  scause: 0x%lx ", scause);
    
    switch(scause) {
        case 0xc:
            printf("(Instruction page fault)\n");
            break;
        case 0xd:
            printf("(Load page fault)\n");
            break;
        case 0xf:
            printf("(Store page fault)\n");
            break;
        case 0x2:
            printf("(Illegal instruction)\n");
            break;
        default:
            printf("(Unknown exception)\n");
            break;
    }
    
    printf("  sepc: 0x%lx (instruction address)\n", sepc);
    printf("  stval: 0x%lx (fault address)\n", stval);
    
    if (p) {
        printf("  Process: %s (PID: %d)\n", p->name, p->pid);
        printf("  User SP: 0x%lx\n", p->tf->sp);
        printf("  User RA: 0x%lx\n", p->tf->ra);
        printf("  Heap top: 0x%lx\n", p->heap_top);
        
        // ✅ 检查栈是否有效
        if (p->tf->sp < 0x10000 || p->tf->sp > 0x11000) {
            printf("  ⚠️  Stack pointer looks invalid!\n");
        }
        
        // ✅ 检查程序计数器
        if (sepc == 0) {
            printf("  ⚠️  Program counter is NULL - likely stack corruption!\n");
        }
    }
}

void trap_user_handler()
{
    proc_t* p = myproc();
    if (!p) {
        panic("trap_user_handler: no current process");
    }
    
    uint64 scause = r_scause();
    uint64 sepc = r_sepc();
    uint64 stval = r_stval();
    
    // ✅ 添加基本的完整性检查
    if (!p->tf) {
        panic("trap_user_handler: process has no trapframe");
    }
    
    // ✅ 可选：简化的调试输出
    #ifdef TRAP_DEBUG
    printf("User trap: scause=0x%lx, sepc=0x%lx, pid=%d\n", scause, sepc, p->pid);
    #endif
    
    if (scause == 8) {  // 系统调用
        // ✅ 检查系统调用是否来自有效地址
        if (sepc < 0x1000 || sepc > 0x20000) {
            printf("❌ System call from invalid address: 0x%lx\n", sepc);
            handle_user_trap_error(scause, sepc, stval);
            panic("Invalid system call address");
        }
        
        // 递增EPC跳过ecall指令
        sepc += 4;
        w_sepc(sepc);
        p->tf->epc = sepc;
        
        // ✅ 直接调用系统调用处理器，不输出测试信息
        syscall();
        
        // 返回用户空间
        trap_user_return();
        
    } else if (scause == 3) {  // 断点异常 (ebreak)
        printf("✅ Program completed with ebreak\n");
        printf("Final return value: %ld\n", p->tf->a0);
        
        // 可以选择退出或继续
        printf("System halted.\n");
        while(1) {
            asm volatile("wfi");
        }
        
    } else {
        // ✅ 使用详细的错误处理
        handle_user_trap_error(scause, sepc, stval);
        
        // ✅ 尝试恢复或安全退出
        if (scause == 0xc && sepc == 0) {
            printf("Attempting to terminate process safely...\n");
            p->tf->a0 = -1;  // 设置退出码
            // 可以选择调用 exit 系统调用或直接终止
            printf("Process terminated due to fatal error\n");
            while(1) {
                asm volatile("wfi");
            }
        }
        
        panic("Unexpected user trap");
    } 
}

void trap_user_return()
{
    proc_t* p = myproc();
    if (!p) {
        panic("trap_user_return: no current process");
    }
    
    // ✅ 添加返回前的完整性检查
    if (!p->tf) {
        panic("trap_user_return: no trapframe");
    }
    
    if (!p->pgtbl) {
        panic("trap_user_return: no page table");
    }
    
    // ✅ 检查返回地址是否合理
    if (p->tf->epc == 0) {
        printf("❌ Warning: returning to address 0!\n");
        printf("  This will likely cause an instruction page fault\n");
        printf("  Trapframe state:\n");
        printf("    epc: 0x%lx\n", p->tf->epc);
        printf("    sp: 0x%lx\n", p->tf->sp);
        printf("    ra: 0x%lx\n", p->tf->ra);
        panic("Invalid return address");
    }
    
    if (p->tf->epc < 0x1000 || p->tf->epc > 0x20000) {
        printf("❌ Warning: suspicious return address 0x%lx\n", p->tf->epc);
    }
    
    // ✅ 直接返回用户空间，不输出调试信息
    extern void user_return(uint64 trapframe, uint64 user_satp);
    user_return((uint64)p->tf, MAKE_SATP(p->pgtbl));
    
    panic("user_return returned - this should never happen");
}
