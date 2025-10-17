//定义中断向量表结构和中断描述符

#ifndef __INTERRUPT_VECTOR_H__
#define __INTERRUPT_VECTOR_H__

#include "interrupt_types.h"
#include "lib/lock.h"
#include "common.h"

// 中断处理函数类型
typedef void (*interrupt_handler_t)(void);

// 中断描述符结构
typedef struct interrupt_desc {
    interrupt_handler_t handler;              // 主中断处理函数
    char *name;                               // 中断名称（调试用）
    int priority;                             // 中断优先级
    int enabled;                              // 中断是否使能
    uint64 count;                             // 中断计数（统计用）
    void *private_data;                       // 私有数据指针
    struct shared_interrupt_node *shared_list; // 共享中断链表头
    int is_shared;                            // 是否为共享中断
    int shared_count;                         // 共享处理函数数量
} interrupt_desc_t;

// 中断向量表结构
typedef struct interrupt_vector_table {
    interrupt_desc_t entries[MAX_INTERRUPTS];  // 中断描述符数组
    spinlock_t lock;                           // 保护向量表的锁
    int nested_level;                          // 中断嵌套层数
    int nested_enabled;                        // 是否允许中断嵌套
    int max_nested_level;                      // 最大嵌套层数限制
} interrupt_vector_table_t;

// 全局中断向量表
extern interrupt_vector_table_t interrupt_table;

#endif
