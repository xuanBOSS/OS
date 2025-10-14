#ifndef __TIMER_H__
#define __TIMER_H__

#include "lib/lock.h"
#include "common.h"

#ifndef SIP_SSIP
#define SIP_SSIP (1L << 1)  // S模式软件中断挂起位
#endif

#ifndef TIMER_FREQ_HZ
#define TIMER_FREQ_HZ 10000000L  // 10MHz时钟频率
#endif


// 计时器
typedef struct timer {
    uint64 ticks;
    spinlock_t lk;
} timer_t;

// 每隔INTERVAL个单位时间发生一次时钟中断(1e6大约为0.1s)
#define INTERVAL 10000000

void   timer_init();       // 时钟初始化(in M-mode)

void   timer_create();     // 时钟创建
void   timer_update();     // 时钟更新(ticks++)
uint64 timer_get_ticks();  // 获取时钟的tick

// 时间获取和计算
uint64 get_time(void);              // 获取当前时间（直接读RISC-V time寄存器）
uint64 timer_get_uptime_ms(void);   // 获取系统运行时间（毫秒）

#endif
