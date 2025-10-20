#ifndef __TIMER_SCHED_H__
#define __TIMER_SCHED_H__

#include "common.h"

// === 配置1：快速滴答（用于验收演示） ===
#define SCHED_TIMER_FREQ_HZ     400         // 400Hz = 2.5ms间隔 (很快)
#define SCHED_TIMER_INTERVAL_MS   2.5       // 2.5毫秒  
#define SCHED_TIMER_INTERVAL_US   2500      // 2500微秒

/* 
// === 配置2：正常滴答 ===
#define SCHED_TIMER_FREQ_HZ     100         // 100Hz = 10ms间隔
#define SCHED_TIMER_INTERVAL_MS   10        // 10毫秒  
#define SCHED_TIMER_INTERVAL_US   10000     // 10000微秒

// === 配置3：慢速滴答 ===
#define SCHED_TIMER_FREQ_HZ     25          // 25Hz = 40ms间隔 (较慢)
#define SCHED_TIMER_INTERVAL_MS   40        // 40毫秒  
#define SCHED_TIMER_INTERVAL_US   40000     // 40000微秒
*/

// 时钟统计信息
struct timer_stats {
    uint64 total_ticks;                 // 总时钟滴答数
    uint64 timer_interrupts;            // 时钟中断总数
    uint64 last_timer_time;             // 上次时钟中断时间
    uint64 timer_interval_cycles;       // 时钟间隔(cycles)
    uint64 timer_frequency;             // 时钟频率
    int timer_enabled;                  // 时钟是否启用
};

// Per-CPU时钟状态
struct cpu_timer_state {
    uint64 local_ticks;                 // 本CPU时钟滴答
    uint64 next_timer_time;             // 下次时钟中断时间
    int timer_depth;                    // 时钟中断嵌套深度
};

// 全局时钟管理器
extern struct timer_stats global_timer_stats;
extern struct cpu_timer_state cpu_timer_states[NCPU];

// 函数声明
void software_timer_check(void);
void software_clockintr(int cpuid);
extern volatile int global_interrupt_count;
void safe_yield_point(void);

void timer_sched_init(void);
void timer_sched_inithart(void);
void timer_sched_interrupt_handler(void);
void timer_update_stats(void);
void print_timer_stats(void);

// SBI接口声明
extern void sbi_set_timer(uint64 time);
extern uint64 get_time(void);

void clockintr(void);

#endif
