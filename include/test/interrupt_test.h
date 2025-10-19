#ifndef INTERRUPT_TEST_H
#define INTERRUPT_TEST_H

#include "common.h"

// ========== 外部变量声明（用extern） ==========
extern volatile int timer_interrupt_count;
extern volatile int cpu_interrupt_counts[NCPU];
extern volatile uint64 last_interrupt_time;

// ========== 函数声明（只有声明，没有实现） ==========
void test_interrupt_setup(void);
void test_timer_interrupt(void);
void test_multi_cpu_interrupts(void);
void test_multi_cpu_interrupts_secondary(void);
void test_interrupt_overhead(void);
void test_interrupt_frequency_impact(void);
void test_exception_handling(void);
void test_interrupt_nesting(void);
void test_error_recovery(void);

// 主测试函数
void run_all_interrupt_tests(void);

// 辅助函数
void reset_interrupt_counters(void);
void update_interrupt_stats(void);
void test_software_interrupt_trigger(void);

// 错误恢复相关函数
int check_stack_overflow(void);
int get_current_interrupt_depth(void);
void print_interrupt_stack_info(void);
void interrupt_stack_enter(void);
void interrupt_stack_exit(void);

#endif // INTERRUPT_TEST_H