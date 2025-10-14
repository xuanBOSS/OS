// 定义接口

#ifndef __TRAP_FRAMEWORK_H__
#define __TRAP_FRAMEWORK_H__

#include "interrupt_vector.h"

#ifndef SIP_SSIP
#define SIP_SSIP (1L << 1)  // S模式软件中断挂起位
#endif

// 中断处理函数类型
typedef void (*interrupt_handler_t)(void);

// 最大嵌套层数
#define MAX_INTERRUPT_NESTING 3

// 扩展标志位
#define IRQ_FLAG_SHARED    (1 << 0)  // 共享中断
#define IRQ_FLAG_FAST      (1 << 1)  // 快速处理
#define IRQ_FLAG_CRITICAL  (1 << 2)  // 关键中断

// 中断配置结构
typedef struct interrupt_config{
    int irq;
    interrupt_handler_t handler;
    const char *name;
    int priority;
    int flags;  //扩展标志位
}interrupt_config_t;

// ====== 核心接口 ======
void trap_init(void);                                    // 初始化中断系统
int register_interrupt(int irq, interrupt_handler_t h);  // 注册中断处理函数
int enable_interrupt(int irq);                           // 开启特定中断
int disable_interrupt(int irq);                          // 关闭特定中断

// ====== 扩展接口 ======
void trap_inithart(void);                               // 每个CPU核心初始化
int unregister_interrupt(int irq);                      // 注销中断处理函数
int set_interrupt_priority(int irq, int priority);      // 设置中断优先级

// ====== 中断嵌套控制 ======
void enable_interrupt_nesting(void);                    // 允许中断嵌套
void disable_interrupt_nesting(void);                   // 禁止中断嵌套

// ====== 中断统计接口 ======
uint64 get_interrupt_count(int irq);                    // 获取中断次数
void print_interrupt_stats(void);                       // 打印中断统计信息

// ====== 内部处理函数 ======
void handle_interrupt(int irq);                         // 通用中断处理
void handle_interrupt_with_priority(int irq);           // 优先级感知中断处理
void fast_interrupt_handler(int irq);                   // 快速中断处理

// ====== 共享中断支持 ======
int register_shared_interrupt(int irq, interrupt_handler_t handler, 
                             const char *name, int priority);
void handle_shared_interrupt(int irq);

// ====== 批量操作接口 ======
int register_interrupt_batch(interrupt_config_t *configs, int count);
void batch_update_interrupt_stats(void);

// ====== 初始化辅助函数 ======
void basic_timer_handler(void);
void setup_basic_timer_interrupt(void);
void uart_interrupt_handler(void);
void add_uart_interrupt_support(void);
void initialize_interrupt_system(void);

#include "trap/trapframe.h"

// 栈管理函数
int get_current_interrupt_depth(void);
void print_interrupt_stack_info(void);

// 中断处理入口（使用trapframe）
void kerneltrap(struct trapframe *tf);

#endif
