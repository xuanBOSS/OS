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
    // ✅ 第一件事：切换到内核中断向量
    extern void kernel_vector();
    w_stvec((uint64)kernel_vector);
    
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
    
    // ✅ 判断是中断还是异常
    if (scause & (1UL << 63)) {
        // 中断处理
        int irq = scause & 0xff;
        
        if (irq == 5) {
            // S-mode timer interrupt
            timer_interrupt_handler();
            // 中断返回时不需要修改EPC，直接返回用户空间
            trap_user_return();
            return;
        } else if (irq == 9) {
            // S-mode external interrupt (磁盘I/O完成等)
            printf("trap_user_handler: external interrupt (irq=9), calling external_interrupt_handler\n");
            external_interrupt_handler();
            printf("trap_user_handler: external_interrupt_handler returned\n");
            // 中断返回时不需要修改EPC，直接返回用户空间
            trap_user_return();
            return;
        } else if (irq == 1) {
            // S-mode software interrupt
            w_sip(r_sip() & ~SIP_SSIP);
            timer_interrupt_handler();
            trap_user_return();
            return;
        } else {
            printf("trap_user_handler: Unknown interrupt %d (scause=0x%lx)\n", irq, scause);
            // 对于未知中断，也尝试返回用户空间
            trap_user_return();
            return;
        }
    }
    
    // 异常处理
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
        
        // ✅ 注意：如果系统调用导致进程 sleep，控制流会通过 sleep()->sched() 切换到调度器
        // 只有当进程被唤醒并重新调度时，才会继续执行
        // 因此这里不需要检查 PROC_SLEEPING 状态
        
        // ✅ 返回用户空间前恢复用户向量
        trap_user_return();
        
    } else if (scause == 3) {  // 断点异常
        printf("✅ Program completed with ebreak\n");
        printf("Final return value: %ld\n", p->tf->a0);
        
        printf("System halted.\n");
        while(1) {
            asm volatile("wfi");
        }
        
    } else {
        // 打印详细的 trap 信息
        printf("trap_user_handler: unhandled trap type: scause=0x%lx\n", scause);
        handle_user_trap_error(scause, sepc, stval);
        
        // 对于某些可恢复的异常，尝试跳过指令继续执行
        uint64 trap_id = scause & 0xf;
        if (trap_id == 0xc || trap_id == 0xd || trap_id == 0xf) {
            // 页错误 - 检查地址是否在有效范围内
            if (sepc < 0x1000 || sepc > 0x20000) {
                // 无效地址 - 进程可能已损坏，终止它
                printf("trap_user_handler: invalid page fault address 0x%lx, terminating process\n", sepc);
                printf("  Process trapframe state: epc=0x%lx, sp=0x%lx, ra=0x%lx\n", 
                       p->tf->epc, p->tf->sp, p->tf->ra);
                
                // 标记进程为已杀死，设置退出码并让进程退出
                p->killed = 1;
                p->exit_code = -1;
                
                // 设置进程状态为 ZOMBIE，这样父进程可以收集它
                spinlock_acquire(&p->lock);
                p->state = PROC_ZOMBIE;
                spinlock_release(&p->lock);
                
                // 唤醒父进程（如果有）
                if (p->parent) {
                    wakeup(p->parent);
                }
                
                // 切换到调度器，永不返回
                sched();
                panic("sched returned");
            } else {
                // 页错误 - 这不应该发生，但我们可以尝试跳过
                printf("trap_user_handler: page fault at valid address 0x%lx, skipping instruction\n", sepc);
                p->tf->epc = sepc + 4;
                trap_user_return();
                return;
            }
        } else if (trap_id == 0x2) {
            // 非法指令 - 检查地址
            if (sepc < 0x1000 || sepc > 0x20000) {
                printf("trap_user_handler: illegal instruction at invalid address 0x%lx, terminating process\n", sepc);
                p->killed = 1;
                p->exit_code = -1;
                
                spinlock_acquire(&p->lock);
                p->state = PROC_ZOMBIE;
                spinlock_release(&p->lock);
                
                if (p->parent) {
                    wakeup(p->parent);
                }
                
                sched();
                panic("sched returned");
            } else {
                printf("trap_user_handler: illegal instruction, skipping\n");
                p->tf->epc = sepc + 4;
                trap_user_return();
                return;
            }
        } else {
            panic("Unexpected user trap");
        }
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
    
    // 计算 user_return 在 TRAMPOLINE 空间中的虚拟地址
    extern char trampoline[];
    extern char user_return[];
    
    uint64 user_vector_offset = (uint64)user_vector - (uint64)trampoline;
    uint64 user_trap_addr = TRAMPOLINE + user_vector_offset;
    
    w_stvec(user_trap_addr);
    
    // ✅ 设置 sscratch
    w_sscratch(TRAPFRAME);
    
    uint64 user_satp = MAKE_SATP(p->pgtbl);
    
    uint64 user_return_offset = (uint64)user_return - (uint64)trampoline;
    uint64 user_return_va = TRAMPOLINE + user_return_offset;
    
    void (*fn)(uint64, uint64) = (void (*)(uint64, uint64))user_return_va;
    fn(TRAPFRAME, user_satp);
    
    panic("user_return returned - this should never happen");
}
