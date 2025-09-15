#include "riscv.h"
#include "dev/uart.h"
#include "proc/proc.h"

// 全局状态变量，用于多核同步
volatile static int boot_cpu_id = -1;
volatile static int init_phase = 0;  // 0: 未初始化, 1: 初始化完成, 2: 允许输出
volatile static int cpu_started[NCPU] = {0};

// 安全的字符串输出函数
void safe_print_string(const char *str) {
    for (int i = 0; str[i]; i++) {
        uart_putc_sync(str[i]);
        // 每个字符后短暂延迟
        for (volatile int j = 0; j < 1000; j++) {
            asm volatile("nop");
        }
    }
}

// 安全的数字输出函数
void safe_print_cpu_id(int cpuid) {
    uart_putc_sync('0' + cpuid);
}

int main()
{
    int cpuid = mycpuid();
    
    // 第一阶段：选择启动CPU并初始化
    if (__sync_bool_compare_and_swap(&boot_cpu_id, -1, cpuid)) {
        // 这个CPU成为启动CPU
        uart_init();
        
        // 长延迟确保UART完全初始化
        for (volatile int i = 0; i < 15000000; i++) {
            asm volatile("nop");
        }
        
        safe_print_string("RISC-V OS starting...\n");
        safe_print_string("CPU ");
        safe_print_cpu_id(cpuid);
        safe_print_string(": Boot CPU initializing...\n");
        safe_print_string("Three core boot completed!\n");
        
        // 标记初始化完成，允许其他CPU继续
        __sync_synchronize();
        init_phase = 1;
        cpu_started[cpuid] = 1;
        
        // 等待所有CPU都启动完成
        while (1) {
            int all_started = 1;
            for (int i = 0; i < NCPU; i++) {
                if (!cpu_started[i]) {
                    all_started = 0;
                    break;
                }
            }
            if (all_started) break;
            
            for (volatile int i = 0; i < 100000; i++) {
                asm volatile("nop");
            }
        }
        
    } else {
        // 非启动CPU等待初始化完成
        while (init_phase == 0) {
            __sync_synchronize();
            for (volatile int i = 0; i < 10000; i++) {
                asm volatile("nop");
            }
        }
        
        // 根据CPU ID添加不同延迟，确保顺序输出
        for (volatile int i = 0; i < (cpuid * 20000000); i++) {
            asm volatile("nop");
        }
        
        // 输出启动信息
        safe_print_string("CPU ");
        safe_print_cpu_id(cpuid);
        safe_print_string(": Secondary CPU started!\n");
        
        // 标记当前CPU已启动
        __sync_synchronize();
        cpu_started[cpuid] = 1;
    }
    
    // 所有CPU进入主循环
    while (1) {
        asm volatile("wfi");
    }
}
