#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/virtio.h"
#include "dev/vio.h"
#include "dev/plic.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "memlayout.h"
#include "riscv.h"

// 中断信息
char* interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 异常信息
char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// 外部函数声明
extern void kernel_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    printf("Initializing kernel trap system\n");
    printf("Kernel trap system initialized\n");
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    int cpuid = mycpuid();
    
    // 设置内核中断向量
    w_stvec((uint64)kernel_vector);
    
    printf("CPU %d: stvec=0x%lx\n", cpuid, r_stvec());
    printf("CPU %d: sie=0x%lx\n", cpuid, r_sie());
    printf("CPU %d: kernel trap initialized\n", cpuid);
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    int irq = plic_claim();
    
    if (irq == UART_IRQ) {
        uart_intr();
    } else if (irq == 1 || irq == 2 || irq == 3 || irq == 8) {
        virtio_disk_intr();
    } else {
        printf("external_interrupt_handler: unhandled irq=%d\n", irq);
    }
    
    if (irq > 0) {
        plic_complete(irq);
    }
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    printf("Timer interrupt handled\n");
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 scause = r_scause();
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 stval = r_stval();

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interrupt enabled");

    // 判断是中断还是异常
    if (scause & (1UL << 63)) {
        // 中断处理
        int irq = scause & 0xff;
        
        if (irq == 5) {
            // S-mode timer interrupt
            timer_interrupt_handler();
        } else if (irq == 9) {
            // S-mode external interrupt
            external_interrupt_handler();  // ← 这里会调用 virtio_disk_intr()
        } else if (irq == 1) {
            // S-mode software interrupt
            w_sip(r_sip() & ~SIP_SSIP);
            timer_interrupt_handler();
        } else {
            printf("CPU %d: Unknown interrupt %d\n", mycpuid(), irq);
        }
    } else {
        // 异常处理
        int trap_id = scause & 0xf;
        const char *name = (trap_id < 16) ? exception_info[trap_id] : "Unknown";
        
        printf("CPU %d: Exception %d (%s) at PC=0x%lx, stval=0x%lx\n", 
               mycpuid(), trap_id, name, sepc, stval);
        
        switch (trap_id) {
            case 2:  // Illegal instruction
                printf("Illegal instruction, skipping\n");
                w_sepc(sepc + 4);
                break;
                
            case 3:  // Breakpoint
                printf("Breakpoint hit, continuing\n");
                w_sepc(sepc + 4);
                break;
                
            case 8:  // Environment call from U-mode
                printf("❌ ERROR: User mode syscall in kernel trap handler!\n");
                panic("User syscall in kernel trap handler");
                break;
                
            case 9:  // Environment call from S-mode
                printf("System call from supervisor mode\n");
                w_sepc(sepc + 4);
                break;
                
            default:
                printf("Unhandled exception - system halted\n");
                printf("sepc=0x%lx, stval=0x%lx, scause=0x%lx\n", sepc, stval, scause);
                while(1) asm volatile("wfi");
        }
    }
}
