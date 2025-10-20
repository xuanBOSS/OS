#include "dev/timer_sched.h"
#include "lib/print.h"
#include "riscv.h"
#include "proc/proc.h"
#include "trap/trap.h"
#include "dev/timer.h"
#include "proc/scheduler.h"

// 添加 CLINT 基地址定义
#define CLINT_BASE 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT_BASE + 0xbff8)

// 全局时钟统计
struct timer_stats global_timer_stats = {0};
struct cpu_timer_state cpu_timer_states[NCPU] = {0};

// 时钟间隔（cycles）
static uint64 timer_interval_cycles = 0;

// 添加全局测试变量
volatile int global_interrupt_count = 0;

void software_timer_check(void) {
    // 检查所有CPU的时钟状态
    for (int cpu = 0; cpu < NCPU; cpu++) {
        uint64 current_time = get_time();
        
        // 检查该CPU是否到了时钟时间
        if (current_time >= cpu_timer_states[cpu].next_timer_time) {
            printf("Software timer triggered for CPU %d\n", cpu);
            
            // 调用时钟中断处理函数（传入CPU ID）
            software_clockintr(cpu);
        }
    }
}

void software_clockintr(int cpuid) {
    printf("CPU %d: Clock interrupt triggered!\n", cpuid);
    
    // === 1. 更新系统时间 ===
    if (cpuid == 0) {
        global_timer_stats.total_ticks++;
        global_interrupt_count++;
        printf("Global tick: %lu\n", global_timer_stats.total_ticks);
    }
    
    cpu_timer_states[cpuid].local_ticks++;
    global_timer_stats.timer_interrupts++;
    
    // === 2. 处理定时器事件（简化）===
    // 不在每次时钟中断都更新时间片
    if (global_timer_stats.total_ticks % 10 == 0) {
        update_process_timeslice();
    }
    
    // === 3. 触发任务调度（简化）===
    // 只在特定条件下触发调度
    if (global_timer_stats.total_ticks % 20 == 0) {  // 每20个时钟中断检查一次
        trigger_reschedule();
    }
    
    // === 4. 设置下次中断时间 ===
    uint64 current_time = get_time();
    uint64 next_time = current_time + timer_interval_cycles;
    cpu_timer_states[cpuid].next_timer_time = next_time;
    
    printf("CPU %d: Next software timer set for %lu\n", cpuid, next_time);
}

// 添加安全调度检查点
void safe_yield_point(void) {
    int cpuid = mycpuid();
    
    // 检查是否需要调度
    if (cpu_scheduler_states[cpuid].need_reschedule) {
        printf("CPU %d: Safe yield point - checking schedule\n", cpuid);
        
        // 确保不在中断上下文中
        if (should_reschedule() && !in_interrupt_context()) {
            yield();  // 执行调度
        } else {
            printf("CPU %d: Schedule deferred - not safe\n", cpuid);
            // 保持调度标志，等待下次安全点
        }
    }
}

// 初始化时钟调度模块
void timer_sched_init(void) {
    printf("=== Timer Scheduler Initialization ===\n");
    
    // 计算时钟间隔
    uint64 cpu_freq = 10000000;  // 10MHz
    timer_interval_cycles = cpu_freq / SCHED_TIMER_FREQ_HZ;
    
    printf("CPU Frequency: %lu Hz\n", cpu_freq);
    printf("Scheduler Timer Frequency: %d Hz\n", SCHED_TIMER_FREQ_HZ);
    printf("Timer Cycles per interrupt: %lu cycles\n", timer_interval_cycles);
    
    // 初始化全局统计
    global_timer_stats.timer_frequency = SCHED_TIMER_FREQ_HZ;
    global_timer_stats.timer_interval_cycles = timer_interval_cycles;
    global_timer_stats.timer_enabled = 1;
    global_timer_stats.total_ticks = 0;
    global_timer_stats.timer_interrupts = 0;
    
    // 初始化所有CPU状态
    for (int i = 0; i < NCPU; i++) {
        cpu_timer_states[i].local_ticks = 0;
        cpu_timer_states[i].timer_depth = 0;
        cpu_timer_states[i].next_timer_time = 0;
    }
    
    printf("Timer scheduler initialized successfully\n");
    printf("==========================================\n");
}

// 每个CPU的时钟初始化
void timer_sched_inithart(void) {
    int cpuid = mycpuid();
    printf("CPU %d: Initializing SOFTWARE timer scheduler\n", cpuid);
    
    // 为每个CPU设置不同的起始时间，避免同时触发
    uint64 current_time = get_time();
    uint64 offset = cpuid * (timer_interval_cycles / 4);  // 错开时间
    uint64 next_time = current_time + timer_interval_cycles + offset;
    
    cpu_timer_states[cpuid].next_timer_time = next_time;
    cpu_timer_states[cpuid].local_ticks = 0;
    
    printf("CPU %d: Software timer set for time %lu (current: %lu, offset: %lu)\n", 
           cpuid, next_time, current_time, offset);
    printf("CPU %d: Timer scheduler initialized\n", cpuid);
}

// 时钟中断处理函数
void clockintr(void) {
    software_clockintr(mycpuid());
}

// 兼容性包装
void timer_sched_interrupt_handler(void) {
    clockintr();
}

// 其他函数保持不变...
void timer_update_stats(void) {
    global_timer_stats.timer_interrupts++;
}

void print_timer_stats(void) {
    printf("\n=== Timer Statistics ===\n");
    printf("Global Timer Stats:\n");
    printf("  Total ticks: %lu\n", global_timer_stats.total_ticks);
    printf("  Timer interrupts: %lu\n", global_timer_stats.timer_interrupts);
    printf("  Timer frequency: %lu Hz\n", global_timer_stats.timer_frequency);
    printf("  Timer enabled: %s\n", global_timer_stats.timer_enabled ? "Yes" : "No");
    
    printf("Per-CPU Timer Stats:\n");
    for (int i = 0; i < NCPU; i++) {
        printf("  CPU %d: %lu ticks, depth=%d, next=%lu\n",
               i,
               cpu_timer_states[i].local_ticks,
               cpu_timer_states[i].timer_depth,
               cpu_timer_states[i].next_timer_time);
    }
    printf("========================\n");
}
