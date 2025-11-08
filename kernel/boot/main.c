// kernel/main.c - 完整的初始化版本
#include "riscv.h"
#include "common.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "proc/proc.h"
#include "proc/cpu.h"
#include "mem/pmem.h"  
#include "lib/print.h" 
#include "mem/kvm.h"   
#include "trap/trap.h"
#include "mem/str.h" 
#include "memlayout.h"

// kernel/main.c - 修复格式化问题
int main()
{
    printf("🎉 Successfully switched to S-mode!\n");
    printf("CPU: %d\n", mycpuid());
    
    // ✅ 修复：使用 %lx 而不是 %p 来显示地址
    printf("\n=== Verifying External Symbols ===\n");
    printf("KERNEL_DATA: 0x%lx\n", (uint64)KERNEL_DATA);
    printf("ALLOC_BEGIN: 0x%lx\n", (uint64)ALLOC_BEGIN);  
    printf("ALLOC_END: 0x%lx\n", (uint64)ALLOC_END);
    printf("trampoline: 0x%lx\n", (uint64)trampoline);
    
    // 检查内存范围合理性
    if ((uint64)ALLOC_END <= (uint64)ALLOC_BEGIN) {
        panic("main: invalid memory range - ALLOC_END <= ALLOC_BEGIN");
    }
    
    uint64 total_memory = (uint64)ALLOC_END - (uint64)ALLOC_BEGIN;
    printf("Total allocatable memory: %ld MB\n", total_memory / (1024*1024));
    
    // === Lab4: 完整的系统初始化序列 ===
    
    printf("\n=== System Initialization Sequence ===\n");
    
    // 1. 首先初始化物理内存管理器
    printf("1. Initializing physical memory manager...\n");
    pmem_init();
    
    // 2. 然后初始化内核虚拟内存
    printf("2. Initializing kernel virtual memory...\n");
    kvm_init();
    
    // 3. 在当前CPU启用内核页表
    printf("3. Enabling kernel page table on CPU %d...\n", mycpuid());
    kvm_inithart();
    
    // ✅ 添加：验证页表切换是否成功
    printf("4. Kernel page table enabled successfully!\n");
    
    // 4. 验证内存管理器
    printf("\n=== Memory Manager Status ===\n");
    printf("Available kernel pages: %d\n", pmem_available(true));
    printf("Available user pages: %d\n", pmem_available(false));
    
    // 5. 验证内核页表
    printf("\n=== Kernel Page Table Status ===\n");
    extern pagetable_t kernel_pagetable;
    if (!kernel_pagetable) {
        panic("main: kernel_pagetable still not initialized");
    }
    printf("Kernel page table: 0x%lx ✅\n", (uint64)kernel_pagetable);
    
    // 6. 初始化进程管理系统
    printf("\n=== Initializing Process Management ===\n");
    proc_init();
    
    // 7. 设置用户态陷阱向量
    // 7. 设置用户态陷阱向量
    printf("Setting up user trap vector...\n");
    extern char trampoline[], user_vector[];
    
    // ✅ 修复：先验证偏移是否正确
    uint64 trampoline_base = (uint64)trampoline;
    uint64 user_vector_addr = (uint64)user_vector;
    uint64 vector_offset = user_vector_addr - trampoline_base;
    
    printf("Debug: trampoline_base=0x%lx, user_vector_addr=0x%lx\n", 
           trampoline_base, user_vector_addr);
    printf("Debug: vector_offset=0x%lx\n", vector_offset);
    
    // 计算在用户页表中的陷阱向量地址
    uint64 user_trap_addr = TRAMPOLINE + vector_offset;
    
    w_stvec(user_trap_addr);
    printf("User trap vector set to: 0x%lx\n", user_trap_addr);
    
    // 8. 验证关键地址
    printf("\nVerifying addresses:\n");
    printf("  trampoline: 0x%lx\n", (uint64)trampoline);
    printf("  user_vector: 0x%lx\n", (uint64)user_vector);
    printf("  TRAMPOLINE: 0x%lx\n", TRAMPOLINE);
    printf("  TRAPFRAME: 0x%lx\n", TRAPFRAME);
    
    printf("\n=== All Subsystems Initialized Successfully ===\n");
    
    // 9. 只有 CPU 0 创建第一个进程
    if (mycpuid() == 0) {
        printf("\n=== Creating First User Process ===\n");
        proc_make_first();
        
        // 如果到达这里说明出错了
        panic("main: proc_make_first() returned unexpectedly");
    } else {
        // 其他 CPU 进入等待状态
        printf("CPU %d: entering idle loop\n", mycpuid());
        while(1) {
            asm volatile("wfi");
        }
    }
    
    return 0;
}
