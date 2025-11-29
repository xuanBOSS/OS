// kernel/main.c - 完整的初始化版本
#include "riscv.h"
#include "common.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "dev/plic.h"
#include "proc/proc.h"
#include "proc/cpu.h"
#include "proc/scheduler.h"
#include "mem/pmem.h"  
#include "lib/print.h" 
#include "mem/kvm.h"   
#include "trap/trap.h"
#include "mem/str.h" 
#include "memlayout.h"
#include "syscall/syscall.h"
#include "fs/file.h"     
#include "fs/fs.h"
#include "fs/inode.h"
#include "fs/buf.h"
#include "dev/vio.h"

// ✅ 添加函数声明
void syscall_init(void);

int main()
{
    w_tp(0);
    
    printf("S-OK\n");
    
    // ✅ 诊断：检查中断使能状态
    uint64 sstatus = r_sstatus();
    uint64 sie = r_sie();
    
    printf("Initial interrupt state:\n");
    printf("  sstatus: 0x%lx (SIE=%d)\n", sstatus, !!(sstatus & SSTATUS_SIE));
    printf("  sie: 0x%lx (SEIE=%d, STIE=%d, SSIE=%d)\n", 
           sie, 
           !!(sie & SIE_SEIE),
           !!(sie & SIE_STIE),
           !!(sie & SIE_SSIE));
    
    // ✅ 重新使能
    w_sie(sie | SIE_SEIE | SIE_STIE | SIE_SSIE);
    
    // 验证
    sie = r_sie();
    printf("After re-enable:\n");
    printf("  sie: 0x%lx (SEIE=%d)\n", sie, !!(sie & SIE_SEIE));

    printf("🎉 Successfully switched to S-mode!\n");
    printf("CPU: %d\n", mycpuid());
    
    printf("\n=== Verifying External Symbols ===\n");
    printf("KERNEL_DATA: 0x%lx\n", (uint64)KERNEL_DATA);
    printf("ALLOC_BEGIN: 0x%lx\n", (uint64)ALLOC_BEGIN);  
    printf("ALLOC_END: 0x%lx\n", (uint64)ALLOC_END);
    printf("trampoline: 0x%lx\n", (uint64)trampoline);
    
    if ((uint64)ALLOC_END <= (uint64)ALLOC_BEGIN) {
        panic("main: invalid memory range");
    }
    
    uint64 total_memory = (uint64)ALLOC_END - (uint64)ALLOC_BEGIN;
    printf("Total allocatable memory: %ld MB\n", total_memory / (1024*1024));
    
    printf("\n=== System Initialization Sequence ===\n");
    
    // 1. 物理内存管理器
    printf("1. Initializing physical memory manager...\n");
    pmem_init();
    
    // 2. 内核虚拟内存
    printf("2. Initializing kernel virtual memory...\n");
    kvm_init();
    
    // 3. 启用内核页表
    printf("3. Enabling kernel page table on CPU %d...\n", mycpuid());
    kvm_inithart();
    printf("4. Kernel page table enabled successfully!\n");
    
    // 4. 验证内存
    printf("\n=== Memory Manager Status ===\n");
    printf("Available kernel pages: %d\n", pmem_available(true));
    printf("Available user pages: %d\n", pmem_available(false));
    
    printf("\n=== Kernel Page Table Status ===\n");
    extern pagetable_t kernel_pagetable;
    if (!kernel_pagetable) {
        panic("main: kernel_pagetable not initialized");
    }
    printf("Kernel page table: 0x%lx ✅\n", (uint64)kernel_pagetable);
    
    // 5. CPU系统
    printf("\n=== Initializing CPU System ===\n");
    cpu_init();

    // ✅ 5.5 初始化中断控制器
    printf("\n=== Initializing Interrupt Controller ===\n");
    plic_init();
    plic_inithart();
printf("PLIC configuration:\n");
printf("  UART priority: %d\n", *(uint32*)(PLIC_PRIORITY(UART_IRQ)));
printf("  VIRTIO priority: %d\n", *(uint32*)(PLIC_PRIORITY(VIRTIO_IRQ)));
printf("  S-mode enable: 0x%x\n", *(uint32*)PLIC_SENABLE(0));
printf("  S-mode threshold: %d\n", *(uint32*)PLIC_SPRIORITY(0));
printf("PLIC initialized and interrupts enabled\n");
    printf("PLIC initialized and interrupts enabled\n");

    printf("\n=== Enabling Interrupts ===\n");
    intr_on();
    printf("Interrupts enabled (sstatus.SIE=1)\n");
    
    // ✅ 6. 文件系统完整初始化
    printf("\n=== Initializing File System ===\n");
    
    // 6.1 初始化虚拟磁盘（必须在 buf_init 之前）
    printf("Initializing virtio disk...\n");
    virtio_disk_init();
    
    // 6.2 初始化文件系统
    //     fs_init() 内部会调用：
    //     - buf_init()
    //     - log_init()
    //     - inode_init()
    //     - file_init()
    //     - dcache_init()
    printf("Initializing filesystem...\n");
    fs_init();
    
    printf("File system initialization complete!\n");
    
    // 7. 系统调用框架
    printf("\n=== Initializing System Call Framework ===\n");
    syscall_init();

    // 8. 进程管理
    printf("\n=== Initializing Process Management ===\n");
    proc_init();

    // 9. 调度器
    printf("\n=== Initializing Scheduler ===\n");
    scheduler_init();
    scheduler_inithart();
    
    // 10. 用户态陷阱向量
    printf("Setting up user trap vector...\n");
    extern char trampoline[], user_vector[];
    
    uint64 trampoline_base = (uint64)trampoline;
    uint64 user_vector_addr = (uint64)user_vector;
    uint64 vector_offset = user_vector_addr - trampoline_base;
    
    printf("Debug: trampoline_base=0x%lx, user_vector_addr=0x%lx\n", 
           trampoline_base, user_vector_addr);
    printf("Debug: vector_offset=0x%lx\n", vector_offset);
    
    if (vector_offset == 0) {
        panic("Invalid trampoline layout");
    }

    uint64 user_trap_addr = TRAMPOLINE + vector_offset;
    w_stvec(user_trap_addr);
    printf("User trap vector set to: 0x%lx\n", user_trap_addr);
    
    printf("\nVerifying addresses:\n");
    printf("  trampoline: 0x%lx\n", (uint64)trampoline);
    printf("  user_vector: 0x%lx\n", (uint64)user_vector);
    printf("  TRAMPOLINE: 0x%lx\n", TRAMPOLINE);
    printf("  TRAPFRAME: 0x%lx\n", TRAPFRAME);
    
    printf("\n=== All Subsystems Initialized Successfully ===\n");
    
    // 11. 创建第一个进程（只在 CPU 0）
    if (mycpuid() == 0) {
        printf("\n=== Creating First User Process ===\n");
        
        printf("\n=== System Status Summary ===\n");
        printf("Physical Memory:\n");
        printf("  Kernel pages available: %d\n", pmem_available(true));
        printf("  User pages available: %d\n", pmem_available(false));
        
        printf("File System:\n");
        printf("  Max files: %d\n", NFILE);
        printf("  Max FDs per process: %d\n", NOFILE);
        printf("  Max devices: %d\n", NDEV);
        
        printf("Process System:\n");
        printf("  Max processes: %d\n", MAX_PROC);
        printf("  Current CPU: %d\n", mycpuid());
        
        cpu_stats();
        
        printf("\n=== Ready to Start First Process ===\n");
        proc_make_first();
        
        printf("\n=== Starting Scheduler ===\n");
        scheduler();
        
        panic("main: scheduler returned unexpectedly");
    } else {
        printf("CPU %d: starting scheduler\n", mycpuid());
        scheduler();
    }
    
    return 0;
}
