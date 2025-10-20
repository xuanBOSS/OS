#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "memlayout.h"
#include "riscv.h"
#include "trap/trap_framework.h"
#include "trap/trapframe.h"
#include "dev/timer_sched.h"
#include "trap/exception.h"
#include "test/interrupt_test.h"

// 添加缺少的常量定义
#ifndef SCAUSE_INTERRUPT
#define SCAUSE_INTERRUPT (1L << 63)
#endif

extern void kernelvec(void);  // 声明汇编中的kernelvec函数

// 栈管理相关定义 
#define STACK_FRAME_SIZE     512     

// 栈管理变量
static int interrupt_stack_depth[NCPU] = {0};

// 栈溢出检查函数
int check_stack_overflow(void) {
    int cpuid = mycpuid();
    
    // 简化的栈检查 - 检查嵌套深度
    if (interrupt_stack_depth[cpuid] >= MAX_STACK_DEPTH) {
        return -2;  // 嵌套过深
    }
    
    return 0;  // 正常
}

// 中断嵌套管理
void interrupt_stack_enter(void) {
    int cpuid = mycpuid();
    
    // 检查栈溢出
    if (check_stack_overflow() != 0) {
        panic("Interrupt stack overflow or nesting too deep");
    }
    
    // 增加嵌套深度
    interrupt_stack_depth[cpuid]++;
    
    // 记录当前栈使用情况（调试用）
    if (interrupt_stack_depth[cpuid] > 1) {
        printf("Nested interrupt level: %d\n", interrupt_stack_depth[cpuid]);
    }
}

void interrupt_stack_exit(void) {
    int cpuid = mycpuid();
    if (interrupt_stack_depth[cpuid] > 0) {
        interrupt_stack_depth[cpuid]--;
    }
}

// 中断信息
/*
static char* interrupt_info[16] = {
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
};*/

// 异常信息
static char* exception_info[16] = {
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

// 异常处理函数
void handle_exception(struct trapframe *tf, int exception_code) {
    // 添加更多调试信息
    printf("=== ENTER handle_exception ===\n");
    printf("Exception code: %d\n", exception_code);
    printf("Exception name: %s\n", exception_info[exception_code]);
    printf("sepc=0x%lx stval=0x%lx sstatus=0x%lx\n", 
           tf->sepc, tf->stval, tf->sstatus);
    
    // 特别处理断点异常
    if (exception_code == 3) {  // 断点异常
        printf("=== BREAKPOINT EXCEPTION DETECTED ===\n");
        printf("Before: sepc = 0x%lx\n", tf->sepc);
        
        // 跳过 ebreak 指令
        tf->sepc += 4;
        
        printf("After: sepc = 0x%lx\n", tf->sepc);
        printf("=== BREAKPOINT HANDLED, RETURNING ===\n");
        return;
    }
    
    // 基本的异常处理
    switch(exception_code) {
        case 8:  // Environment call from U-mode
        case 9:  // Environment call from S-mode  
            printf("System call not implemented\n");
            break;
        case 12: // Instruction page fault
        case 13: // Load page fault
        case 15: // Store/AMO page fault
            printf("Page fault at address: 0x%lx\n", tf->stval);
            break;
        default:
            printf("Unhandled exception\n");
            break;
    }
    
    printf("=== EXIT handle_exception ===\n");
}

// trapframe调试输出
void dump_trapframe(struct trapframe *tf) {
    printf("=== Trapframe Dump ===\n");
    printf("CSR Registers:\n");
    printf("  sepc: 0x%lx\n", tf->sepc);
    printf("  sstatus: 0x%lx\n", tf->sstatus);
    printf("  scause: 0x%lx\n", tf->scause);
    printf("  stval: 0x%lx\n", tf->stval);
    
    printf("General Purpose Registers:\n");
    for(int i = 0; i < 32; i += 4) {
        printf("  x%d-x%d: 0x%lx 0x%lx 0x%lx 0x%lx\n", 
               i, i+3, tf->reg[i], tf->reg[i+1], tf->reg[i+2], tf->reg[i+3]);
    }
    printf("=== End Trapframe ===\n");
}

// 打印尺寸的辅助函数
void print_size64(uint64 size_bytes) {
    if (size_bytes < 1024) {
        printf("%lu bytes", size_bytes);
    } else if (size_bytes < 1024 * 1024) {
        printf("%lu KB", size_bytes / 1024);
    } else {
        printf("%lu MB", size_bytes / (1024 * 1024));
    }
}

// in trap.S
// 内核中断处理流程
extern void kernel_vector();
extern void timer_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    printf("Initializing trap system...\n");
    
    // 原有初始化
    plic_init();
    timer_init();
    timer_create();
    
    // 添加新的异常处理初始化
    exception_init();
    
    printf("Trap system initialized successfully\n");
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    // 设置内核中断向量
    w_stvec((uint64)kernelvec);
    
    printf("CPU %d: kernelvec address = 0x%lx\n", mycpuid(), (uint64)kernelvec);
    printf("CPU %d: stvec set to: 0x%lx\n", mycpuid(), r_stvec());
    
    int cpuid = mycpuid();
    
    // === 添加PLIC hart初始化 ===
    printf("CPU %d: Initializing PLIC hart...\n", cpuid);
    plic_inithart();
    printf("CPU %d: PLIC hart initialized\n", cpuid);
    
    printf("CPU %d: Setting SIE register...\n", cpuid);
    
    uint64 sie_value = SIE_SSIE | SIE_STIE | SIE_SEIE;
    w_sie(sie_value);
    
    uint64 sie_read = r_sie();
    printf("CPU %d: After w_sie(0x%lx), read back: 0x%lx\n", 
           cpuid, sie_value, sie_read);
    
    // 设置 SPP 为 S-mode
    uint64 sstatus = r_sstatus();
    printf("CPU %d: Current sstatus: 0x%lx (SPP: %s)\n", 
           cpuid, sstatus, (sstatus & SSTATUS_SPP) ? "S-mode" : "U-mode");
    
    sstatus |= SSTATUS_SPP;
    sstatus |= SSTATUS_SIE;
    w_sstatus(sstatus);
    
    uint64 sstatus_after = r_sstatus();
    printf("CPU %d: After SPP fix: 0x%lx (SPP: %s)\n", 
           cpuid, sstatus_after, (sstatus_after & SSTATUS_SPP) ? "S-mode" : "U-mode");
    
    printf("CPU %d: Final status - sie=0x%lx, sstatus=0x%lx\n", 
           cpuid, r_sie(), sstatus_after);
    
    printf("CPU %d: trap handler initialized\n", cpuid);
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    printf("Handling external interrupt...\n");
    
    // 获取中断号
    int irq = plic_claim();
    printf("PLIC claimed IRQ: %d\n", irq);
    
    if(irq == UART_IRQ) {
        printf("UART interrupt received\n");
        uart_intr_with_debug();  // 使用带调试的版本
    } else if(irq) {
        printf("Unexpected external interrupt: irq=%d\n", irq);
    }
    
    // 通知PLIC中断处理完成
    if(irq) {
        plic_complete(irq);
        printf("PLIC completed IRQ: %d\n", irq);
    }
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler_original()
{
    // S模式软件中断由M模式timer_vector触发，清除标志
    w_sip(r_sip() & ~SIP_SSIP);
    
    // 1. 更新系统时间
    timer_update();
}

// 新的集成时钟中断处理函数
void timer_interrupt_handler_integrated()
{
    // 调用原有的处理
    timer_interrupt_handler_original();
    
    // 2. 新增：调用调度器的时钟处理
    timer_sched_interrupt_handler();  // 重命名以避免冲突
}

int devintr_check(void) {
    uint64 scause = r_scause();
    
    if (scause == 0x8000000000000005L) {
        printf("T");  // 添加这行：直接在这里输出T
        //printf("Timer interrupt detected!\n");  // 调试信息
        
        update_interrupt_stats();
        clockintr();
        return 2;
    } else if (scause == 0x8000000000000009L) {
        printf("External interrupt detected!\n");
        external_interrupt_handler();  // 调用完整的外部中断处理
        return 1;
    } else if (scause == 0x8000000000000001L) {
        printf("Software interrupt detected!\n");
        return 1;
    }
    
    return 0;
}

void kerneltrap(struct trapframe *tf)
{
    //uint64 sepc = tf->sepc;           
    uint64 sstatus = tf->sstatus;    
    uint64 scause = tf->scause;      

    // 简化调试输出，只在非时钟中断时打印
    if (scause != 0x8000000000000005L) {
        printf(">>> KERNELTRAP: scause=0x%lx, CPU=%d <<<\n", scause, mycpuid());
    }

    assert(sstatus & SSTATUS_SPP, "kerneltrap: not from s-mode");
    assert(intr_get() == 0, "kerneltrap: interrupt enabled");

    interrupt_stack_enter();

    // 判断是中断还是异常
    if(scause & SCAUSE_INTERRUPT) {
        int which_dev = devintr_check();
        if (which_dev == 0 && scause != 0x8000000000000005L) {
            printf("Unknown interrupt: 0x%lx\n", scause);
        }
    } else {
        printf("Processing exception: 0x%lx\n", scause);
        handle_exception(tf, scause & 0xf);
    }
    
    interrupt_stack_exit();
    
    if (scause != 0x8000000000000005L) {
        printf(">>> KERNELTRAP COMPLETE <<<\n");
    }
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 构造一个临时trapframe
    struct trapframe tf;
    tf.sepc = sepc;
    tf.sstatus = sstatus;
    tf.scause = scause;
    tf.stval = stval;
    
    // 调用新的处理函数
    kerneltrap(&tf);
    
    // 恢复寄存器状态
    w_sepc(sepc);
    w_sstatus(sstatus);    
}

// ====== 任务4公共接口 ======

// 栈状态查询函数
int get_current_interrupt_depth(void) {
    int cpuid = mycpuid();
    return interrupt_stack_depth[cpuid];
}

void print_interrupt_stack_info(void) {
    printf("Interrupt Stack Info:\n");
    for (int i = 0; i < NCPU; i++) {
        printf("  CPU %d: depth = %d\n", i, interrupt_stack_depth[i]);
    }
}
