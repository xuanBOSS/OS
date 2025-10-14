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

// 添加缺少的常量定义
#ifndef SCAUSE_INTERRUPT
#define SCAUSE_INTERRUPT (1L << 63)
#endif

// ====== 任务4：上下文保存与恢复相关 ======

// 栈管理相关定义
#define STACK_GUARD_SIZE     4096    
#define MAX_STACK_DEPTH      8       
#define STACK_FRAME_SIZE     512     

// 每CPU栈状态跟踪
static int interrupt_stack_depth[NCPU] = {0};

// 栈溢出检查函数
static int check_stack_overflow(void) {
    int cpuid = mycpuid();
    
    // 简化的栈检查 - 检查嵌套深度
    if (interrupt_stack_depth[cpuid] >= MAX_STACK_DEPTH) {
        return -2;  // 嵌套过深
    }
    
    return 0;  // 正常
}

// 中断嵌套管理
static void interrupt_stack_enter(void) {
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

static void interrupt_stack_exit(void) {
    int cpuid = mycpuid();
    if (interrupt_stack_depth[cpuid] > 0) {
        interrupt_stack_depth[cpuid]--;
    }
}

// 中断信息
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
};

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
static void handle_exception(struct trapframe *tf, int exception_code) {
    printf("Kernel Exception: %s\n", exception_info[exception_code]);
    printf("sepc=0x%lx stval=0x%lx sstatus=0x%lx\n", 
           tf->sepc, tf->stval, tf->sstatus);
    
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
    
    // 简单处理：终止或跳过指令
    panic("Unhandled kernel exception");
}

// in trap.S
// 内核中断处理流程
extern void kernel_vector();
extern void timer_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    printf("Initializing trap system...\n");
    
    // 初始化PLIC
    plic_init();
    
    // 初始化定时器
    timer_init();
    
    // 创建系统时钟
    timer_create();
    
    printf("Trap system initialized successfully\n");
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    int cpuid = mycpuid();
    printf("CPU %d: initializing trap handler\n", cpuid);
    
    // 设置S模式中断向量
    w_stvec((uint64)kernel_vector);
    
    // 初始化PLIC for this hart
    plic_inithart();
    
    // 使能S模式外部中断和软件中断
    w_sie(r_sie() | SIE_SEIE | SIE_SSIE);
    
    // 开启中断
    intr_on();
    
    printf("CPU %d: trap handler initialized\n", cpuid);
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    printf("Handling external interrupt...\n");
    
    // 获取中断号
    int irq = plic_claim();
    
    if(irq == UART_IRQ) {
        printf("UART interrupt received\n");
        uart_intr();  // 处理UART中断
    } else if(irq) {
        printf("Unexpected external interrupt: irq=%d\n", irq);
    }
    
    // 通知PLIC中断处理完成
    if(irq) {
        plic_complete(irq);
    }
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    // S模式软件中断由M模式timer_vector触发，清除标志
    w_sip(r_sip() & ~SIP_SSIP);
    
    // 1. 更新系统时间
    timer_update();

}

// ====== 任务4：新的trapframe处理入口 ======

// 使用trapframe的新中断处理入口
void kerneltrap(struct trapframe *tf)
{
    __attribute__((unused)) uint64 sepc = tf->sepc;           
    uint64 sstatus = tf->sstatus;    
    uint64 scause = tf->scause;      
    __attribute__((unused)) uint64 stval = tf->stval;           

    // 基本检查
    assert(sstatus & SSTATUS_SPP, "kerneltrap: not from s-mode");
    assert(intr_get() == 0, "kerneltrap: interrupt enabled");

    // 栈管理 - 任务4新增
    interrupt_stack_enter();

    int trap_id = scause & 0xf; 

    // 判断是中断还是异常
    if(scause & SCAUSE_INTERRUPT) {
        // 中断处理 - 使用我们的框架
        printf("Kernel Interrupt: %s\n", interrupt_info[trap_id]);
        
        switch(trap_id) {
            case 1: // S-mode软件中断（时钟中断）
                timer_interrupt_handler();  // 使用新的处理函数
                break;
            case 9: // S-mode外部中断
                handle_interrupt(IRQ_S_EXT);
                break;
            default:
                printf("Unknown interrupt: scause=0x%lx\n", scause);
                break;
        }
    } else {
        // 异常处理 - 任务4新增
        handle_exception(tf, trap_id);
    }
    
    // 栈管理 - 任务4新增
    interrupt_stack_exit();
    
    // trapframe中的sepc和sstatus可能被修改，无需手动恢复
    // 汇编代码会从trapframe恢复所有状态
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
