#include "proc/scheduler.h"
#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "proc/cpu.h"

// 简单的自旋锁实现
struct spinlock {
    volatile int locked;
    char* name;
};

// 调度器锁
static struct spinlock scheduler_lock;

// 每CPU调度器状态
struct scheduler_state cpu_scheduler_states[NCPU];

// 调度器全局统计 - 注意这里要匹配头文件中的声明
struct scheduler_statistics scheduler_stats;

// 简单的锁实现
void initlock(struct spinlock *lk, char *name) {
    lk->locked = 0;
    lk->name = name;
}

void acquire(struct spinlock *lk) {
    while (__sync_lock_test_and_set(&lk->locked, 1)) {
        asm volatile("nop");
    }
    __sync_synchronize();
}

void release(struct spinlock *lk) {
    __sync_synchronize();
    __sync_lock_release(&lk->locked);
}

// === 调度器初始化 ===
void scheduler_init(void) {
    printf("=== Scheduler Initialization ===\n");
    
    // 初始化调度器锁
    initlock(&scheduler_lock, "scheduler");
    
    // 清零统计信息
    scheduler_stats.total_schedules = 0;
    scheduler_stats.voluntary_yields = 0;
    scheduler_stats.preemptive_schedules = 0;
    scheduler_stats.timeslice_expires = 0;
    
    printf("Scheduler initialized successfully\n");
}

void scheduler_inithart(void) {
    int cpuid = mycpuid();
    
    // 初始化本CPU的调度器状态
    cpu_scheduler_states[cpuid].need_reschedule = 0;
    cpu_scheduler_states[cpuid].last_schedule_time = r_time();
    cpu_scheduler_states[cpuid].timeslice_length = 100000; // 10ms @ 10MHz
    cpu_scheduler_states[cpuid].in_scheduler = 0;
}

// === 调度时机判断 ===
int should_reschedule(void) {
    int cpuid = mycpuid();
    
    // 1. 检查是否在中断上下文中
    if (in_interrupt_context()) {
        return 0;  // 不在中断中调度
    }
    
    // 2. 检查是否已在调度器中
    if (cpu_scheduler_states[cpuid].in_scheduler) {
        return 0;  // 避免递归调度
    }
    
    return 1;  // 可以安全调度
}

// === 调度触发逻辑 ===
void trigger_reschedule(void) {
    int cpuid = mycpuid();
    
    // 避免在中断上下文中直接调度
    if (in_interrupt_context()) {
        cpu_scheduler_states[cpuid].need_reschedule = 1;
        return;
    }
    
    cpu_scheduler_states[cpuid].need_reschedule = 1;
}

// === 调度原子性保证 ===
void scheduler_lock_acquire(void) {
    acquire(&scheduler_lock);
}

void scheduler_lock_release(void) {
    release(&scheduler_lock);
}

int in_interrupt_context(void) {
    // 检查是否在时钟中断处理中
    // int cpuid = mycpuid();
    
    // 简单的检查：如果当前CPU正在处理时钟中断，则认为在中断上下文
    // 可以通过检查调用栈或设置标志位来实现
    
    // 方法1：检查 SCAUSE 寄存器是否显示中断
    uint64 scause = r_scause();
    if (scause & (1ULL << 63)) {  // 最高位为1表示中断
        return 1;  // 在中断上下文中
    }
    
    return 0;  // 不在中断上下文中
}

// === 主动让出CPU ===
void yield(void) {
    scheduler_stats.voluntary_yields++;
    
    // 确保调度的原子性
    if (!should_reschedule()) {
        return;
    }
    
    // 调用调度器
    schedule();
}

// === 核心调度函数 ===
void schedule(void) {
    int cpuid = mycpuid();
    
    // === 确保调度原子性 ===
    uint64 old_sstatus = r_sstatus();
    intr_off();  // 关闭中断
    
    // 获取调度器锁
    scheduler_lock_acquire();
    
    // 标记进入调度器
    cpu_scheduler_states[cpuid].in_scheduler = 1;
    
    // 更新统计信息
    scheduler_stats.total_schedules++;
    cpu_scheduler_states[cpuid].last_schedule_time = r_time();
    
    // === 这里将来会有真正的进程切换逻辑 ===
    // proc_t* current = myproc();
    // if (current) {
    //     swtch(&current->ctx, &next_proc->ctx);
    // }
    
    // 重置调度标志
    cpu_scheduler_states[cpuid].need_reschedule = 0;
    
    // === 退出调度器 ===
    cpu_scheduler_states[cpuid].in_scheduler = 0;
    
    scheduler_lock_release();
    
    // 恢复中断状态
    w_sstatus(old_sstatus);
}

// === 更新进程时间片 ===
void update_process_timeslice(void) {
   // 将来实现进程时间片管理
}

// === 调度器统计信息 ===
void print_scheduler_stats(void) {
    printf("\n=== Scheduler Statistics ===\n");
    printf("Total schedules: %lu\n", scheduler_stats.total_schedules);
    printf("Voluntary yields: %lu\n", scheduler_stats.voluntary_yields);
    printf("Preemptive schedules: %lu\n", scheduler_stats.preemptive_schedules);
    printf("Timeslice expires: %lu\n", scheduler_stats.timeslice_expires);
    
    for (int i = 0; i < NCPU; i++) {
        printf("CPU %d: need_reschedule=%d, in_scheduler=%d\n", 
               i, cpu_scheduler_states[i].need_reschedule,
               cpu_scheduler_states[i].in_scheduler);
    }
    printf("=============================\n");
}
