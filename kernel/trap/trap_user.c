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
    
    if (!p->tf) {
        panic("trap_user_handler: process has no trapframe");
    }
    
    if (scause == 8) {  // 系统调用
        if (sepc < 0x1000 || sepc > 0x20000) {
            printf("❌ System call from invalid address: 0x%lx\n", sepc);
            handle_user_trap_error(scause, sepc, stval);
            panic("Invalid system call address");
        }
        
        // 递增EPC跳过ecall指令
        sepc += 4;
        w_sepc(sepc);
        p->tf->epc = sepc;
        
        // 调用系统调用处理器
        syscall();
        
        // ✅ 关键修改：检查进程状态
        if (p->state == PROC_SLEEPING) {
            printf("trap_user_handler: process went to sleep, calling scheduler\n");
            // 进程进入睡眠状态，需要调度其他进程
            sched();  // 这会切换到调度器
            // 当进程被唤醒时会回到这里
            printf("trap_user_handler: process woke up, returning to user\n");
        }
        
        // 返回用户空间
        trap_user_return();
        
    } else if (scause == 3) {  // 断点异常 (ebreak)
        printf("✅ Program completed with ebreak\n");
        printf("Final return value: %ld\n", p->tf->a0);
        
        printf("System halted.\n");
        while(1) {
            asm volatile("wfi");
        }
        
    } else {
        handle_user_trap_error(scause, sepc, stval);
        panic("Unexpected user trap");
    } 
}

#define USER_RETURN_OFFSET ((uint64)user_return - (uint64)trampoline)
#define USER_RETURN_VA (TRAMPOLINE + USER_RETURN_OFFSET)

void trap_user_return()
{
    proc_t* p = myproc();
    if (!p) {
        panic("trap_user_return: no current process");
    }
    
    printf("trap_user_return: ENTRY - PID=%d\n", p->pid);
    
    uint64 user_satp = MAKE_SATP(p->pgtbl);
    
    // ✅ 计算 user_return 在 TRAMPOLINE 空间中的虚拟地址
    extern char trampoline[];
    extern char user_return[];
    
    uint64 user_return_offset = (uint64)user_return - (uint64)trampoline;
    uint64 user_return_va = TRAMPOLINE + user_return_offset;
    
    printf("trap_user_return: trampoline PA = 0x%lx\n", (uint64)trampoline);
    printf("trap_user_return: user_return PA = 0x%lx\n", (uint64)user_return);
    printf("trap_user_return: user_return offset = 0x%lx\n", user_return_offset);
    printf("trap_user_return: user_return VA = 0x%lx\n", user_return_va);
    
    printf("trap_user_return: calling user_return via trampoline\n");
    
    // ✅ 通过函数指针调用 user_return 的虚拟地址版本
    void (*fn)(uint64, uint64) = (void (*)(uint64, uint64))user_return_va;
    fn(TRAPFRAME, user_satp);
    
    panic("user_return returned - this should never happen");
}
