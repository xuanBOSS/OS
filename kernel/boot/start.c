#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h" 

__attribute__ ((aligned (4096))) uint8 CPU_stack[4096 * NCPU];

// 添加M模式初始化函数
void mmode_init(void) {
    printf("=== M-mode initialization ===\n");
    
    // === 关键修复：精确设置中断委托位 ===
    // RISC-V 标准中断位：
    // bit 1: SSIE (supervisor software interrupt)
    // bit 5: STIE (supervisor timer interrupt)  
    // bit 9: SEIE (supervisor external interrupt)
    // bit 7: MTIE (machine timer interrupt) ← 这个是关键！
    
    uint64 mideleg_value = (1L << 1) |  // SSIE
                           (1L << 5) |  // STIE  
                           (1L << 9) |  // SEIE
                           (1L << 7);   // MTIE - 关键添加！
    
    w_mideleg(mideleg_value);
    uint64 actual_mideleg = r_mideleg();
    printf("M-mode: interrupt delegation = 0x%lx\n", actual_mideleg);
    
    // 检查 MTIE 是否成功委托
    if (actual_mideleg & (1L << 7)) {
        printf("M-mode: ✓ MTIE successfully delegated to S-mode\n");
    } else {
        printf("M-mode: ✗ MTIE delegation failed\n");
    }
    
    // 委托异常给S模式（保持原样）
    w_medeleg(0xbfff);  // 不用 0xffff，用标准值
    printf("M-mode: exception delegation = 0x%lx\n", r_medeleg());
    
    // 启用S模式中断
    w_sie(SIE_SEIE | SIE_STIE | SIE_SSIE);
    printf("M-mode: SIE = 0x%lx\n", r_sie());
    
    // === 关键：启用M模式时钟中断 ===
    uint64 mie_value = r_mie() | MIE_MTIE;  // 使用 MTIE 而不是 STIE
    w_mie(mie_value);
    printf("M-mode: MIE = 0x%lx\n", r_mie());
    
    // 启用stimecmp扩展（如果支持）
    uint64 menvcfg = r_menvcfg() | (1L << 63);
    w_menvcfg(menvcfg);
    printf("M-mode: MENVCFG = 0x%lx\n", r_menvcfg());
    
    // 允许S模式访问时间寄存器
    w_mcounteren(r_mcounteren() | MCOUNTEREN_TM);
    printf("M-mode: MCOUNTEREN = 0x%lx\n", r_mcounteren());
    
    printf("M-mode initialization completed\n");
}

// 添加时钟初始化函数
void timerinit(void) {
    printf("Initializing timer...\n");
    
    // 设置第一个时钟中断（更短的间隔用于测试）
    uint64 current = r_time();
    uint64 first_timer = current + 100000;  // 100K cycles ≈ 10ms at 10MHz
    
    // 同时设置 CLINT 和 stimecmp
    w_stimecmp(first_timer);
    
    // 也设置 CLINT mtimecmp（如果需要）
    volatile uint64 *mtimecmp = (uint64*)(0x2000000L + 0x4000 + 8 * mycpuid());
    *mtimecmp = first_timer;
    
    printf("First timer set for time %lu (current: %lu)\n", 
           first_timer, current);
}

void start()
{
    // === 新增：M模式初始化 ===
    mmode_init();
    timerinit();
    
    // 允许所有CPU执行到main函数
    extern int main();
    main();
}