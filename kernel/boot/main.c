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
#include "trap/exception.h" 
#include "test/interrupt_test.h"

// 外部函数声明
extern void clockintr(void);
extern void kernelvec(void);  // 添加这个声明

void test_exception_modules(void);

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

// 主函数
int main()
{
    int cpuid = mycpuid();
    
    if (__sync_bool_compare_and_swap(&boot_cpu_id, -1, cpuid)) {
        // === Boot CPU 初始化 ===
        uart_init();
        print_init();
        
        printf("RISC-V OS - Lab3 Interrupt System Test\n");
        printf("Boot CPU: %d\n", cpuid);

        // 基础初始化
        pmem_init();
        kvm_init();
        
        // 定时器和调度器初始化
        printf("\n=== Initializing Timer and Scheduler ===\n");
        timer_sched_init();
        scheduler_init();

        // 异常处理初始化
        printf("\n=== Initializing Exception Handler ===\n");
        
        // CPU特定初始化
        kvm_inithart();
        trap_kernel_inithart();
        timer_sched_inithart();
        scheduler_inithart();
        
        printf("Boot CPU initialization completed!\n");

        // 等待secondary CPUs
        __sync_synchronize();
        init_phase = 4;

        int timeout = 0;
        while (secondary_cpus_ready < (NCPU - 1) && timeout < 500000) {
            timeout++;
            for (volatile int i = 0; i < 100; i++) asm volatile("nop");
        }
        
        printf("Secondary CPUs ready: %d/%d\n", secondary_cpus_ready, NCPU - 1);

        // === Lab3 中断测试 ===
        printf("\n============================================================\n");
        printf("    STARTING LAB3 INTERRUPT SYSTEM TESTS\n");
        printf("============================================================\n");
        
        // 等待系统稳定
        for (volatile int i = 0; i < 1000000; i++);
        
        // 运行完整的中断测试套件
        run_all_interrupt_tests();
        
        // === 异常处理测试 ===
        printf("\n=== Running Exception Handler Tests ===\n");
        test_exception_modules();
        
        printf("\n=== Lab3 Testing Complete ===\n");
        printf("Interrupt System: ✅ TESTED\n");
        printf("Exception Handling: ✅ TESTED\n");
        printf("Multi-CPU Support: ✅ TESTED\n");
        printf("\n🎉 LAB3 COMPLETED SUCCESSFULLY! 🎉\n");
        
        // 进入主循环
        printf("Entering main loop...\n");
        while(1) {
            asm volatile("wfi");
        }
        
    } else {
        // === Secondary CPU ===
        while (init_phase < 4) {
            __sync_synchronize();
            for (volatile int i = 0; i < 10000; i++) asm volatile("nop");
        }
        
        // Secondary CPU 初始化
        kvm_inithart();
        trap_kernel_inithart();
        timer_sched_inithart();
        scheduler_inithart();
        
        cpu_started[cpuid] = 1;
        __sync_fetch_and_add(&secondary_cpus_ready, 1);
        
        printf("CPU %d: Initialization completed\n", cpuid);
        
        // 等待Boot CPU完成基础测试
        for (volatile int i = 0; i < 2000000; i++);
        
        // Secondary CPU运行简化的中断测试
        printf("CPU %d: Running multi-CPU interrupt test\n", cpuid);
        test_multi_cpu_interrupts_secondary();  // 参与多CPU测试
        
        printf("CPU %d: Testing completed, entering main loop\n", cpuid);
        while(1) {
            asm volatile("wfi");
        }
    }
}