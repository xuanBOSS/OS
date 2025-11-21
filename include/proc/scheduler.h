#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

// 调度器状态
struct scheduler_state {
    int need_reschedule;        // 是否需要重新调度
    uint64 last_schedule_time;  // 上次调度时间
    uint64 timeslice_length;    // 时间片长度
    int in_scheduler;           // 是否在调度器中
};

// 调度器统计信息
struct scheduler_statistics {
    uint64 total_schedules;
    uint64 voluntary_yields;
    uint64 preemptive_schedules;
    uint64 timeslice_expires;
};

// 每CPU调度器状态
extern struct scheduler_state cpu_scheduler_states[NCPU];

// 调度器统计信息（全局）
extern struct scheduler_statistics scheduler_stats;

// 调度器接口函数
void scheduler_init(void);
void scheduler_inithart(void);
void schedule(void);
int should_reschedule(void);
void trigger_reschedule(void);
void update_process_timeslice(void);
void print_scheduler_stats(void);  // 添加这个声明

// 调度原子性保护
void scheduler_lock_acquire(void);
void scheduler_lock_release(void);
int in_interrupt_context(void);

#endif
