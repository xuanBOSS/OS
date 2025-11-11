#include "proc/scheduler.h"
#include "riscv.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "proc/proc.h"
#include "proc/cpu.h"

// 调度器锁 - 使用现有的 spinlock_t 类型
static spinlock_t scheduler_lock;

// 每CPU调度器状态
struct scheduler_state cpu_scheduler_states[NCPU];

// 调度器全局统计 - 注意这里要匹配头文件中的声明
struct scheduler_statistics scheduler_stats;

// === 调度器初始化 ===
void scheduler_init(void) {
    printf("=== Scheduler Initialization ===\n");
    
    // 初始化调度器锁 - 使用现有的接口
    spinlock_init(&scheduler_lock, "scheduler");
    
    // 清零统计信息
    scheduler_stats.total_schedules = 0;
    scheduler_stats.voluntary_yields = 0;
    scheduler_stats.preemptive_schedules = 0;
    scheduler_stats.timeslice_expires = 0;
    
    printf("Scheduler initialized successfully\n");
}

void scheduler_inithart(void) {
    int cpuid = mycpuid();
    printf("scheduler_inithart: starting for CPU %d\n", cpuid);
    
    // 检查CPU ID是否有效
    if (cpuid < 0 || cpuid >= NCPU) {
        printf("scheduler_inithart: invalid CPU ID %d\n", cpuid);
        return;
    }
    
    // 初始化本CPU的调度器状态
    cpu_scheduler_states[cpuid].need_reschedule = 0;
    cpu_scheduler_states[cpuid].last_schedule_time = 0;  // 暂时不使用时间
    cpu_scheduler_states[cpuid].timeslice_length = 100000;
    cpu_scheduler_states[cpuid].in_scheduler = 0;
    
    printf("CPU %d scheduler initialized successfully\n", cpuid);
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
    spinlock_acquire(&scheduler_lock);  // ✅ 使用现有接口
}

void scheduler_lock_release(void) {
    spinlock_release(&scheduler_lock);  // ✅ 使用现有接口
}

int in_interrupt_context(void) {
    // 检查是否在时钟中断处理中
    
    // 方法1：检查 SCAUSE 寄存器是否显示中断
    uint64 scause = r_scause();
    if (scause & (1ULL << 63)) {  // 最高位为1表示中断
        return 1;  // 在中断上下文中
    }
    
    // 方法2：检查当前CPU的中断嵌套深度
    cpu_t* cpu = mycpu();
    if (cpu->noff > 0) {
        return 1;  // 可能在中断处理中
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
    
    printf("schedule: CPU %d entering scheduler\n", cpuid);
    
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
    
    printf("schedule: CPU %d - total schedules: %lu\n", cpuid, scheduler_stats.total_schedules);
    
    // === 这里将来会有真正的进程切换逻辑 ===
    proc_t* current = myproc();
    if (current) {
        printf("schedule: current process: %s (pid=%d)\n", current->name, current->pid);
        // 将来在这里实现进程切换
        // swtch(&current->ctx, &next_proc->ctx);
    } else {
        printf("schedule: no current process\n");
    }
    
    // 重置调度标志
    cpu_scheduler_states[cpuid].need_reschedule = 0;
    
    // === 退出调度器 ===
    cpu_scheduler_states[cpuid].in_scheduler = 0;
    
    scheduler_lock_release();
    
    // 恢复中断状态
    w_sstatus(old_sstatus);
    
    printf("schedule: CPU %d exiting scheduler\n", cpuid);
}

// === 更新进程时间片 ===
void update_process_timeslice(void) {
    int cpuid = mycpuid();
    proc_t* current = myproc();
    
    if (!current) {
        return;
    }
    
    uint64 current_time = r_time();
    uint64 elapsed = current_time - cpu_scheduler_states[cpuid].last_schedule_time;
    
    // 检查时间片是否用完
    if (elapsed >= cpu_scheduler_states[cpuid].timeslice_length) {
        printf("update_process_timeslice: CPU %d timeslice expired for process %s\n", 
               cpuid, current->name);
        scheduler_stats.timeslice_expires++;
        trigger_reschedule();
    }
}

// === 调度器统计信息 ===
void print_scheduler_stats(void) {
    printf("\n=== Scheduler Statistics ===\n");
    printf("Total schedules: %lu\n", scheduler_stats.total_schedules);
    printf("Voluntary yields: %lu\n", scheduler_stats.voluntary_yields);
    printf("Preemptive schedules: %lu\n", scheduler_stats.preemptive_schedules);
    printf("Timeslice expires: %lu\n", scheduler_stats.timeslice_expires);
    
    for (int i = 0; i < NCPU; i++) {
        printf("CPU %d: need_reschedule=%d, in_scheduler=%d, timeslice=%lu\n", 
               i, 
               cpu_scheduler_states[i].need_reschedule,
               cpu_scheduler_states[i].in_scheduler,
               cpu_scheduler_states[i].timeslice_length);
    }
    printf("=============================\n");
}

// ✅ 新增：调度器测试函数
void scheduler_test(void) {
    printf("\n=== Scheduler Test ===\n");
    
    printf("Testing yield()...\n");
    yield();
    
    printf("Testing trigger_reschedule()...\n");
    trigger_reschedule();
    
    printf("Testing should_reschedule()...\n");
    int can_schedule = should_reschedule();
    printf("Can schedule: %s\n", can_schedule ? "yes" : "no");
    
    printf("Testing update_process_timeslice()...\n");
    update_process_timeslice();
    
    print_scheduler_stats();
    
    printf("======================\n");
}
