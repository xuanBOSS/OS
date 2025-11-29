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

    extern void simple_trap();
    w_stvec((uint64)simple_trap);

    // ✅ 修复：正确委托中断
    w_medeleg(0xffff);  // 委托所有异常
    w_mideleg((1 << 1) | (1 << 5) | (1 << 9));  // 委托 SSIP, STIP, SEIP
    printf("M-mode: delegation configured\n");

    // ✅ 验证委托
    uint64 mideleg_val = r_mideleg();
    printf("M-mode: mideleg=0x%lx (SEIP=%d)\n", 
           mideleg_val, !!(mideleg_val & (1 << 9)));

    // 使能 S-mode 中断
    w_sie(SIE_SEIE | SIE_STIE | SIE_SSIE);
    
    // ✅ 验证
    uint64 sie_val = r_sie();
    printf("M-mode: sie=0x%lx\n", sie_val);

    // 配置 PMP
    w_pmpaddr0(0x3fffffffffffffull);
    w_pmpcfg0(0xf);
    printf("M-mode: PMP configured\n");

    w_satp(0);
    
    int id = r_mhartid();
    w_tp(id);

    uint64 mstatus_val = r_mstatus();
    mstatus_val &= ~MSTATUS_MPP_MASK;
    mstatus_val |= MSTATUS_MPP_S;
    w_mstatus(mstatus_val);

    extern void smode_test();
    w_mepc((uint64)smode_test);

    printf("M-mode: ready to switch to S-mode\n");
    asm volatile("mret");
    
    printf("ERROR: mret failed!\n");
    while(1);
}
