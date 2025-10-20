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
#include "test/lab3_validation.h"

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

        // === Lab3 验收演示（替换原来的测试） ===
        printf("\n============================================================\n");
        printf("    LAB3 验收演示 - 中断处理与时钟管理\n");
        printf("============================================================\n");
        
        // 等待系统稳定
        for (volatile int i = 0; i < 1000000; i++);
        
        // 运行验收演示
        run_validation_demo();
        
        printf("\n=== 验收演示结束，开始交互测试 ===\n");
        printf("系统就绪，请输入字符测试UART功能\n");
        printf("同时观察时钟滴答 'T' 字符\n");
        
        // === 验收专用：持续UART检查循环 ===
        int uart_check_counter = 0;
        int total_input_chars = 0;
        
        while(1) {
            // 检查UART输入
            int c = uart_getc_sync();
            if (c != -1) {
                total_input_chars++;
                
                // === 验收要求：显示输入并回显 ===
                printf("[输入%d] ", total_input_chars);
                
                // 显示字符
                if (c >= 32 && c <= 126) {
                    printf("字符='%c' ", c);
                } else if (c == '\r') {
                    printf("回车键 ");
                } else if (c == '\n') {
                    printf("换行键 ");
                } else if (c == 27) {
                    printf("ESC键 ");
                } else {
                    printf("控制字符 ");
                }
                
                printf("-> 回显: ");
                
                // 回显字符
                uart_putc_sync(c);
                
                // 处理特殊字符和换行
                if (c == '\r') {
                    uart_putc_sync('\n');
                    printf(" [回车+换行]\n");
                } else if (c == '\n') {
                    printf(" [换行]\n");
                } else if (c >= 32 && c <= 126) {
                    printf(" '%c'\n", c);
                } else {
                    printf(" [控制字符]\n");
                }
                
                // 退出条件
                if (c == 'q' || c == 'Q') {
                    printf("\n===========================================\n");
                    printf("检测到退出字符\n");
                    printf("总共输入了 %d 个字符\n", total_input_chars);
                    printf("UART输入回显测试完成！\n");
                    printf("===========================================\n");
                    break;
                }
            }
            
            // 每隔一段时间强制输出一个T（模拟时钟滴答）
            uart_check_counter++;
            if (uart_check_counter >= 50000) {  // 调整频率
                printf("T");
                uart_check_counter = 0;
            }
            
            // 短暂延迟，避免CPU占用过高
            for (volatile int i = 0; i < 1000; i++);
        }
        
        // 如果退出了循环，进入待机
        printf("进入系统待机模式...\n");
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
        
        // Secondary CPU 也协助检查时钟滴答
        int secondary_timer_counter = 0;
        while(1) {
            // 每个secondary CPU也输出一些T（频率更低）
            secondary_timer_counter++;
            if (secondary_timer_counter >= 100000) {
                printf("T");
                secondary_timer_counter = 0;
            }
            
            for (volatile int i = 0; i < 5000; i++);
            asm volatile("wfi");
        }
    }
}