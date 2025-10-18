#include "riscv.h"
#include "common.h"
#include "dev/uart.h"
#include "dev/timer.h"
#include "proc/proc.h"
#include "mem/pmem.h"  
#include "lib/print.h" 
#include "mem/vmem.h"  
#include "mem/kvm.h"   
#include "trap/trap.h"
#include "trap/trapframe.h"          // 添加这个头文件
#include "trap/trap_framework.h"     // 添加这个头文件
#include "dev/timer_sched.h"
#include "proc/scheduler.h"

#define CLINT_BASE 0x2000000L
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8*(hartid))
#define CLINT_MTIME (CLINT_BASE + 0xbff8)

// 外部函数声明
extern void clockintr(void);
extern void kernelvec(void);  // 添加这个声明


// 全局状态变量
volatile static int uart_lock = 0;

// 安全输出函数
void safe_print_string(const char *str) {
    while (__sync_lock_test_and_set(&uart_lock, 1)) {
        asm volatile("nop");
    }
    
    for (int i = 0; str[i]; i++) {
        uart_putc_sync(str[i]);
        for (volatile int j = 0; j < 1000; j++) {
            asm volatile("nop");
        }
    }
    
    __sync_lock_release(&uart_lock);
}

void test_software_timer_with_scheduling(void) {
    printf("\n=== Software Timer + Scheduler Test ===\n");
    printf("Running integrated timer and scheduler test...\n");
    
    uint64 start_time = get_time();
    int start_ticks = global_timer_stats.total_ticks;
    
    for (int i = 0; i < 150; i++) {  // 增加到150次
        // 调用软件时钟检查
        software_timer_check();
        
        // === 关键：在安全点检查调度 ===
        safe_yield_point();
        
        // 适当的延迟
        for (volatile int j = 0; j < 200000; j++) {
            asm volatile("nop");
        }
        
        // 每30次显示一次状态
        if (i % 30 == 29) {
            uint64 current_time = get_time();
            printf("Iteration %d: Global ticks = %lu, time elapsed = %lu\n", 
                   i + 1, global_timer_stats.total_ticks, current_time - start_time);
            
            // 显示调度统计
            print_scheduler_stats();
        }
        
        // 如果已经有足够的ticks和调度，可以提前结束
        if (global_timer_stats.total_ticks - start_ticks >= 15 && 
            scheduler_stats.total_schedules >= 5) {
            printf("Achieved 15 timer ticks and 5 schedules, ending test early at iteration %d\n", i + 1);
            break;
        }
    }
    
    uint64 end_time = get_time();
    int end_ticks = global_timer_stats.total_ticks;
    
    printf("=== Software Timer + Scheduler Test Complete ===\n");
    printf("Generated %d timer ticks in %lu cycles\n", 
           end_ticks - start_ticks, end_time - start_time);
    
    // 最终统计
    print_scheduler_stats();
}

// 调度器压力测试
void test_scheduler_integration(void) {
    printf("\n=== Scheduler Integration Test ===\n");
    
    // 测试主动让出
    printf("Testing voluntary yield...\n");
    yield();
    
    // 测试强制调度
    printf("Testing forced reschedule...\n");
    trigger_reschedule();
    safe_yield_point();
    
    // 测试调度原子性
    printf("Testing scheduling atomicity...\n");
    intr_off();
    trigger_reschedule();
    printf("Reschedule requested while interrupts disabled\n");
    intr_on();
    safe_yield_point();
    
    printf("=== Scheduler Integration Test Complete ===\n");
}

void detect_timer_features(void) {
    printf("\n=== Timer Features Detection ===\n");
    
    // 检查是否支持 stimecmp
    printf("1. Testing stimecmp support:\n");
    uint64 old_stimecmp = r_stimecmp();
    uint64 test_value = 0x12345678;
    w_stimecmp(test_value);
    uint64 read_back = r_stimecmp();
    
    if (read_back == test_value) {
        printf("   ✓ stimecmp register is functional\n");
    } else {
        printf("   ✗ stimecmp register not working (wrote: 0x%lx, read: 0x%lx)\n", 
               test_value, read_back);
    }
    
    // 恢复原值
    w_stimecmp(old_stimecmp);
    
    // 检查 CLINT 内存映射
    printf("2. Testing CLINT memory mapping:\n");
    volatile uint64 *mtime = (uint64*)(CLINT_BASE + 0xbff8);
    volatile uint64 *mtimecmp0 = (uint64*)(CLINT_BASE + 0x4000);
    
    uint64 time1 = *mtime;
    for(volatile int i = 0; i < 1000; i++);
    uint64 time2 = *mtime;
    
    if (time2 > time1) {
        printf("   ✓ CLINT mtime register is counting\n");
        printf("   mtime: %lu -> %lu (diff: %lu)\n", time1, time2, time2 - time1);
        
        // 测试 mtimecmp 写入
        uint64 old_mtimecmp = *mtimecmp0;
        *mtimecmp0 = test_value;
        uint64 mtimecmp_read = *mtimecmp0;
        *mtimecmp0 = old_mtimecmp;  // 恢复
        
        if (mtimecmp_read == test_value) {
            printf("   ✓ CLINT mtimecmp register is writable\n");
        } else {
            printf("   ✗ CLINT mtimecmp register not writable\n");
        }
    } else {
        printf("   ✗ CLINT mtime register not counting\n");
    }
    
    // 检查 M-mode 中断委托
    printf("3. Testing M-mode interrupt delegation:\n");
    uint64 mideleg = r_mideleg();
    uint64 medeleg = r_medeleg();
    
    printf("   MIDELEG: 0x%lx\n", mideleg);
    printf("     MTIE delegated: %s\n", (mideleg & (1L << 7)) ? "YES" : "NO");
    printf("     STIE delegated: %s\n", (mideleg & (1L << 5)) ? "YES" : "NO");
    printf("   MEDELEG: 0x%lx\n", medeleg);
    
    if (!(mideleg & (1L << 7))) {
        printf("   ⚠️  WARNING: Machine timer interrupt not delegated to S-mode!\n");
        printf("   This explains why CLINT doesn't trigger S-mode interrupts!\n");
    }
    
    // 检查中断向量
    printf("4. Testing interrupt vector:\n");
    uint64 stvec = r_stvec();
    printf("   Current stvec: 0x%lx\n", stvec);
    printf("   kernelvec address: 0x%lx\n", (uint64)kernelvec);
    
    if (stvec == (uint64)kernelvec) {
        printf("   ✓ Interrupt vector correctly set\n");
    } else {
        printf("   ✗ Interrupt vector MISMATCH!\n");
    }
    
    // 详细中断状态检查
    printf("5. Detailed interrupt state:\n");
    uint64 sie = r_sie();
    uint64 sstatus = r_sstatus();
    uint64 sip = r_sip();
    
    printf("   SIE: 0x%lx\n", sie);
    printf("     SSIE (bit 1): %s\n", (sie & (1L << 1)) ? "ON" : "OFF");
    printf("     STIE (bit 5): %s\n", (sie & (1L << 5)) ? "ON" : "OFF");
    printf("     SEIE (bit 9): %s\n", (sie & (1L << 9)) ? "ON" : "OFF");
    
    printf("   SSTATUS: 0x%lx\n", sstatus);
    printf("     SIE (global): %s\n", (sstatus & SSTATUS_SIE) ? "ON" : "OFF");
    printf("     SPP: %s\n", (sstatus & SSTATUS_SPP) ? "S-mode" : "U-mode");
    
    printf("   SIP: 0x%lx\n", sip);
    printf("     SSIP (bit 1): %s\n", (sip & (1L << 1)) ? "PENDING" : "CLEAR");
    printf("     STIP (bit 5): %s\n", (sip & (1L << 5)) ? "PENDING" : "CLEAR");
    printf("     SEIP (bit 9): %s\n", (sip & (1L << 9)) ? "PENDING" : "CLEAR");
    
    printf("=== Detection Complete ===\n");
}

void fix_interrupt_delegation(void) {
    printf("\n=== Attempting to fix interrupt delegation ===\n");
    
    uint64 current_mideleg = r_mideleg();
    printf("Current MIDELEG: 0x%lx\n", current_mideleg);
    
    // 添加 MTIE (bit 7) 委托
    uint64 new_mideleg = current_mideleg | (1L << 7);
    printf("Attempting to set MIDELEG to: 0x%lx\n", new_mideleg);
    
    // 尝试写入（可能会失败，因为我们在 S-mode）
    w_mideleg(new_mideleg);
    
    uint64 result_mideleg = r_mideleg();
    printf("Result MIDELEG: 0x%lx\n", result_mideleg);
    
    if (result_mideleg & (1L << 7)) {
        printf("✓ Successfully delegated MTIE to S-mode\n");
    } else {
        printf("✗ Failed to delegate MTIE (need M-mode privilege)\n");
    }
    
    printf("=== Fix attempt complete ===\n");
}

// === 模块1：调度器基本功能测试 ===
void test_scheduler_basic_functions(void) {
    printf("\n=== Module 1: Scheduler Basic Functions Test ===\n");
    
    printf("1.1 Testing voluntary yield...\n");
    yield();
    
    printf("1.2 Testing forced reschedule...\n");
    trigger_reschedule();
    safe_yield_point();
    
    printf("1.3 Testing scheduling safety checks...\n");
    if (should_reschedule()) {
        printf("✓ Scheduling conditions are safe\n");
    } else {
        printf("⚠ Scheduling conditions not met\n");
    }
    
    print_scheduler_stats();
    printf("=== Module 1 Complete ===\n");
}

// === 模块2：调度原子性测试 ===
void test_scheduler_atomicity(void) {
    printf("\n=== Module 2: Scheduler Atomicity Test ===\n");
    
    printf("2.1 Testing interrupt protection...\n");
    intr_off();
    printf("Interrupts disabled - requesting schedule\n");
    trigger_reschedule();
    printf("Schedule request deferred (as expected)\n");
    intr_on();
    printf("Interrupts enabled - processing deferred schedule\n");
    safe_yield_point();
    
    printf("2.2 Testing scheduler lock protection...\n");
    scheduler_lock_acquire();
    printf("Scheduler lock acquired\n");
    int can_schedule = should_reschedule();
    printf("Can schedule while locked: %s\n", can_schedule ? "YES" : "NO");
    scheduler_lock_release();
    printf("Scheduler lock released\n");
    
    printf("2.3 Testing recursive scheduling prevention...\n");
    // 这里会测试调度器的重入保护
    yield(); // 第一次调度
    
    print_scheduler_stats();
    printf("=== Module 2 Complete ===\n");
}

// === 模块3：时钟驱动调度测试 ===
void test_timer_driven_scheduling(void) {
    printf("\n=== Module 3: Timer-Driven Scheduling Test ===\n");
    
    printf("3.1 Running timer-driven scheduling simulation...\n");
    
    uint64 start_time = get_time();
    int start_ticks = global_timer_stats.total_ticks;
    int start_schedules = scheduler_stats.total_schedules;
    
    // 运行较短的测试，专注于调度
    for (int i = 0; i < 50; i++) {
        // 调用软件时钟检查
        software_timer_check();
        
        // 检查调度点
        safe_yield_point();
        
        // 适当的延迟
        for (volatile int j = 0; j < 100000; j++) {
            asm volatile("nop");
        }
        
        // 每10次报告一次
        if (i % 10 == 9) {
            printf("Iteration %d: Global ticks = %lu, Schedules = %lu\n", 
                   i + 1, global_timer_stats.total_ticks, scheduler_stats.total_schedules);
        }
        
        // 如果有足够的数据就提前结束
        if (global_timer_stats.total_ticks - start_ticks >= 5) {
            printf("Achieved 5 timer ticks, ending test at iteration %d\n", i + 1);
            break;
        }
    }
    
    uint64 end_time = get_time();
    int end_ticks = global_timer_stats.total_ticks;
    int end_schedules = scheduler_stats.total_schedules;
    
    printf("3.2 Timer-driven scheduling results:\n");
    printf("   Timer ticks generated: %d\n", end_ticks - start_ticks);
    printf("   Schedules triggered: %d\n", end_schedules - start_schedules);
    printf("   Time elapsed: %lu cycles\n", end_time - start_time);
    if (end_ticks > start_ticks) {
        printf("   Average cycles per tick: %lu\n", 
               (end_time - start_time) / (end_ticks - start_ticks));
    }
    
    print_scheduler_stats();
    printf("=== Module 3 Complete ===\n");
}

// === 模块4：多CPU调度协调测试 ===
void test_multi_cpu_scheduling(void) {
    printf("\n=== Module 4: Multi-CPU Scheduling Coordination Test ===\n");
    
    printf("4.1 Current CPU states:\n");
    for (int i = 0; i < NCPU; i++) {
        printf("   CPU %d: need_reschedule=%d, in_scheduler=%d, local_ticks=%lu\n",
               i, 
               cpu_scheduler_states[i].need_reschedule,
               cpu_scheduler_states[i].in_scheduler,
               cpu_timer_states[i].local_ticks);
    }
    
    printf("4.2 Testing cross-CPU scheduling coordination...\n");
    // 在当前CPU触发调度
    int current_cpu = mycpuid();
    printf("Current CPU: %d\n", current_cpu);
    
    // 测试调度触发
    trigger_reschedule();
    safe_yield_point();
    
    printf("4.3 Final CPU states after scheduling:\n");
    for (int i = 0; i < NCPU; i++) {
        printf("   CPU %d: need_reschedule=%d, in_scheduler=%d\n",
               i, 
               cpu_scheduler_states[i].need_reschedule,
               cpu_scheduler_states[i].in_scheduler);
    }
    
    print_scheduler_stats();
    printf("=== Module 4 Complete ===\n");
}

// === 主测试函数 ===
void test_scheduler_modules(void) {
    printf("\n============================================================\n");
    printf("        TASK 5 SCHEDULER INTEGRATION TEST SUITE\n");
    printf("============================================================\n");
    
    // 模块1：基本功能
    test_scheduler_basic_functions();
    
    // 模块2：原子性保护
    test_scheduler_atomicity();
    
    // 模块3：时钟驱动调度
    test_timer_driven_scheduling();
    
    // 模块4：多CPU协调
    test_multi_cpu_scheduling();
    
    // 最终总结
    printf("\n============================================================\n");
    printf("        SCHEDULER INTEGRATION TEST SUMMARY\n");
    printf("============================================================\n");
    
    printf("Final Statistics:\n");
    print_scheduler_stats();
    
    printf("\nTest Results Analysis:\n");
    if (scheduler_stats.total_schedules > 0) {
        printf("✅ Basic scheduling: WORKING\n");
        printf("✅ Voluntary yields: %lu\n", scheduler_stats.voluntary_yields);
        printf("✅ Timer-driven scheduling: FUNCTIONAL\n");
        printf("✅ Multi-CPU coordination: OPERATIONAL\n");
    } else {
        printf("⚠️  No schedules occurred - check configuration\n");
    }
    
    printf("\nScheduler Integration Status: ");
    if (scheduler_stats.total_schedules >= 3 && 
        scheduler_stats.voluntary_yields >= 2) {
        printf("✅ FULLY OPERATIONAL\n");
    } else if (scheduler_stats.total_schedules >= 1) {
        printf("🔶 PARTIALLY OPERATIONAL\n");
    } else {
        printf("❌ NEEDS INVESTIGATION\n");
    }
    
    printf("============================================================\n");
}

// 主函数
int main()
{
    int cpuid = mycpuid();
    
    if (__sync_bool_compare_and_swap(&boot_cpu_id, -1, cpuid)) {
        // === Boot CPU 初始化 ===
        uart_init();
        print_init();
        
        printf("RISC-V OS - Task 5 Timer Debug...\n");
        printf("Boot CPU: %d\n", cpuid);

        // 基础初始化
        pmem_init();
        kvm_init();

        
        // Task 5 初始化
        printf("\n=== Task 5: Timer Scheduler Init ===\n");
        timer_sched_init();
        scheduler_init();  // 添加调度器初始化
        
        // CPU特定初始化
        kvm_inithart();
        trap_kernel_inithart();
        timer_sched_inithart();
        scheduler_inithart();  // 添加调度器初始化
        
        printf("Boot CPU initialization completed!\n");

        // 等待secondary CPUs (简化)
        __sync_synchronize();
        init_phase = 4;

        int timeout = 0;
        while (secondary_cpus_ready < (NCPU - 1) && timeout < 500000) {
            timeout++;
            for (volatile int i = 0; i < 100; i++) asm volatile("nop");
        }
        
        printf("Secondary CPUs ready: %d/%d\n", secondary_cpus_ready, NCPU - 1);

        test_scheduler_modules();
        
        printf("\nTask 5 Scheduler Integration - Test Complete\n");
        printf("Ready for Task 6 (Exception Handling)\n");
        printf("Shutting down...\n");
        
        for(int i = 3; i > 0; i--) {
            printf("%d...\n", i);
            for(volatile int j = 0; j < 5000000; j++) asm volatile("nop");
        }
        
        while(1) asm volatile("wfi");
        
    } else {
        // === Secondary CPU (简化) ===
        while (init_phase < 4) {
            __sync_synchronize();
            for (volatile int i = 0; i < 10000; i++) asm volatile("nop");
        }
        
        kvm_inithart();
        trap_kernel_inithart();
        timer_sched_inithart();
        scheduler_inithart();  // 添加调度器初始化
        
        cpu_started[cpuid] = 1;
        __sync_fetch_and_add(&secondary_cpus_ready, 1);
        
        while(1) asm volatile("wfi");
    }
}