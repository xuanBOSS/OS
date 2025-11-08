#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"
#include "proc/proc.h"
#include "proc/cpu.h"

/*-------------------- M模式时钟初始化 --------------------*/

// M模式时钟中断处理流程
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间
static uint64 mscratch[NCPU][5];

// 时钟初始化（M模式）
void timer_init()
{
    int hartid = mycpuid();
    
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
}

/*--------------------- S模式系统时钟 --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建（初始化系统时钟）
void timer_create()
{
    sys_timer.ticks = 0;
    spinlock_init(&sys_timer.lk, "timer");
}

// 时钟更新（S模式软件中断时调用）
void timer_update()
{
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
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
