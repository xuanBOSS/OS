#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc/proc.h"
#include "common.h"


/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{
    int hartid = mycpuid();
    
    printf("CPU %d: initializing timer\n", hartid);
    
    // 设置mscratch区域
    // mscratch[hartid][0-2]: 保存a1,a2,a3寄存器
    // mscratch[hartid][3]: CLINT_MTIMECMP地址
    // mscratch[hartid][4]: 时间间隔INTERVAL
    mscratch[hartid][3] = CLINT_MTIMECMP(hartid);
    mscratch[hartid][4] = INTERVAL;
    
    // 设置mscratch寄存器指向当前CPU的存储区域
    w_mscratch((uint64)&mscratch[hartid]);
    
    // 设置M模式中断向量
    w_mtvec((uint64)timer_vector);
    
    // 设置第一次定时器中断
    *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + INTERVAL;
    
    // 使能M模式定时器中断
    w_mie(r_mie() | MIE_MTIE);
    
    printf("CPU %d: timer initialized\n", hartid);
}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    printf("Creating system timer...\n");
    
    sys_timer.ticks = 0;
    spinlock_init(&sys_timer.lk, "timer");
    
    printf("System timer created\n");
}

// 时钟更新(ticks++ with lock)
void timer_update()
{
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;

    // 任务5新增：每100个tick输出一次状态
    if (sys_timer.ticks % 100 == 0) {
        printf("Timer: tick %ld (uptime: %ld ms)\n", 
               sys_timer.ticks, timer_get_uptime_ms());
    }

    spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    spinlock_acquire(&sys_timer.lk);
    uint64 ticks = sys_timer.ticks;
    spinlock_release(&sys_timer.lk);
    return ticks;
}

// 获取当前时间（直接读RISC-V time寄存器）
uint64 get_time(void) {
    return r_time();
}

// 获取系统运行时间（毫秒）
uint64 timer_get_uptime_ms(void) {
    return timer_get_ticks() * (INTERVAL / (TIMER_FREQ_HZ / 1000));
}

// 处理进程的时间片（为调度器提供接口）
void timer_tick_process()
{
    // 留空，暂无调度相关功能
}

void timer_print_stats(void) {
    uint64 ticks = timer_get_ticks();
    uint64 uptime = timer_get_uptime_ms();
    printf("=== Timer Stats ===\n");
    printf("Ticks: %ld, Uptime: %ld ms\n", ticks, uptime);
}
