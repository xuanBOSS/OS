// kernel/boot/start.c - 只做初始化，不切换
#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h" 
#include "proc/cpu.h"

//__attribute__ ((aligned (4096))) uint8 CPU_stack[4096 * NCPU];

// 全局启动控制变量
volatile int cpu_start_barrier = 0;
volatile int current_starting_cpu = 0;

// 声明汇编中的函数
extern void smode_test();

void debug_print_mstatus_before(uint64 mstatus) {
    printf("🔍 mstatus before: 0x%lx\n", mstatus);
}

void debug_print_mstatus_after(uint64 mstatus) {
    printf("🔍 mstatus after:  0x%lx\n", mstatus);
}

void debug_print_mepc(uint64 mepc) {
    printf("🔍 mepc set to:    0x%lx\n", mepc);
}

void debug_print_main_addr(uint64 addr) {
    printf("🔍 main function:  0x%lx\n", addr);
}

void debug_before_mret() {
    printf("🔍 About to execute mret...\n");
}

void debug_mret_failed() {
    printf("❌ mret failed!\n");
    while(1) asm volatile("nop");
}

// start函数 - M-mode初始化
// start函数 - M-mode初始化
void start() {
    printf("M-mode: start() called\n");

    // 🔥 重要：设置 S-mode 异常向量
    extern void simple_trap();
    w_stvec((uint64)simple_trap);

    // 委托中断和异常到S-mode
    w_medeleg(0x3fff);
    w_mideleg(0x1666);
    printf("M-mode: delegation configured\n");

    // 允许S-mode接收定时器、软件和外部中断
    w_sie(SIE_SEIE | SIE_STIE | SIE_SSIE);

    // 配置物理内存保护允许全部访问
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);
    printf("M-mode: PMP configured\n");

    // 禁用分页
    w_satp(0);

    // 设定tp寄存器为当前CPU ID
    int id = r_mhartid();
    w_tp(id);

    // 准备mstatus将切换至S态
    uint64 mstatus_val = r_mstatus();
    mstatus_val &= ~MSTATUS_MPP_MASK;
    mstatus_val |= MSTATUS_MPP_S;
    w_mstatus(mstatus_val);

    // 设置mepc，mret后跳转到smode_test
    extern void smode_test();
    w_mepc((uint64)smode_test);

    printf("M-mode: ready to switch to S-mode\n");
    printf("M-mode: mstatus=0x%lx, mepc=0x%lx\n", r_mstatus(), (uint64)smode_test);

    // 跳转S-mode执行smode_test
    asm volatile("mret");
    
    // 如果执行到这里说明mret失败
    printf("ERROR: mret failed!\n");
    while(1) asm volatile("nop");
}