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
    // 获取当前cpuid
    int hartid = r_tp();

    // 一开始设置 cmp_time = cur_time + time interval
    // 之后每触发一次时钟中断 有 cmp_time += time interval
    *(uint64*)CLINT_MTIMECMP(hartid) = *(uint64*)CLINT_MTIME + INTERVAL;

    // 指向当前CPU的mscratch, 与trap.S里的timer_vector密切配合
    uint64* scratch = mscratch[hartid];
    scratch[3] = CLINT_MTIMECMP(hartid);
    scratch[4] = INTERVAL;
    w_mscratch((uint64)scratch);

    // 设置M-mode时钟中断处理函数
    w_mtvec((uint64)timer_vector);

    // M-mode中断使能(总开关)
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // M-mode中断使能(时钟中断开关)
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
    wakeup(&sys_timer.ticks);
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
