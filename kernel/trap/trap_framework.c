#include "trap/trap_framework.h"
#include "trap/trap_errors.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/plic.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "riscv.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "trap/trapframe.h"

// 共享中断节点池
#define MAX_SHARED_NODES 64  // 最大共享中断节点数
typedef struct shared_interrupt_node {
    interrupt_handler_t handler;
    char *name;
    int priority;
    void *private_data;
    int in_use;                           // 节点是否被使用
    struct shared_interrupt_node *next;   // 链表指针
} shared_interrupt_node_t;

// 静态共享中断节点池
static shared_interrupt_node_t shared_node_pool[MAX_SHARED_NODES];
static spinlock_t shared_pool_lock;
static int shared_pool_initialized = 0;

// 全局中断向量表
interrupt_vector_table_t interrupt_table;

// 调试输出
#define DEBUG_INTERRUPT_VERBOSE 0    // 详细调试信息
#define DEBUG_INTERRUPT_BASIC   1    // 基本调试信息
#define DEBUG_INTERRUPT_ERROR   1    // 错误信息

// 多核相关定义
static volatile int per_cpu_interrupt_depth[NCPU] = {0};
static volatile int per_cpu_interrupt_total[NCPU] = {0};

// 默认的空中断处理函数
static void default_interrupt_handler(void)
{
    printf("Warning: unhandled interrupt occurred\n");
}

// ========================================
// 共享中断节点池管理函数
// ========================================

// 初始化共享中断节点池
static void init_shared_interrupt_pool(void)
{
    if (shared_pool_initialized) return;
    
    spinlock_init(&shared_pool_lock, "shared_pool");
    
    for (int i = 0; i < MAX_SHARED_NODES; i++) {
        shared_node_pool[i].handler = NULL;
        shared_node_pool[i].name = NULL;
        shared_node_pool[i].priority = 0;
        shared_node_pool[i].private_data = NULL;
        shared_node_pool[i].in_use = 0;
        shared_node_pool[i].next = NULL;
    }
    
    shared_pool_initialized = 1;
    printf("Shared interrupt pool initialized (%d nodes available)\n", MAX_SHARED_NODES);
}

// 分配共享中断节点
static shared_interrupt_node_t* alloc_shared_node(void)
{
    spinlock_acquire(&shared_pool_lock);
    
    for (int i = 0; i < MAX_SHARED_NODES; i++) {
        if (!shared_node_pool[i].in_use) {
            shared_node_pool[i].in_use = 1;
            shared_node_pool[i].handler = NULL;
            shared_node_pool[i].name = NULL;
            shared_node_pool[i].priority = 0;
            shared_node_pool[i].private_data = NULL;
            shared_node_pool[i].next = NULL;
            
            spinlock_release(&shared_pool_lock);
            return &shared_node_pool[i];
        }
    }
    
    spinlock_release(&shared_pool_lock);
    return NULL;  // 池已满
}

// 释放共享中断节点
static void free_shared_node(shared_interrupt_node_t *node)
{
    if (!node) return;
    
    spinlock_acquire(&shared_pool_lock);
    node->in_use = 0;
    node->handler = NULL;
    node->name = NULL;
    node->priority = 0;
    node->private_data = NULL;
    node->next = NULL;
    spinlock_release(&shared_pool_lock);
}

// ====== 核心接口实现 ======

// 初始化中断系统
void trap_init(void)
{
    int cpuid = mycpuid();
    
    if (cpuid == boot_cpu_id) {
        // 只有CPU 0执行全局初始化
        printf("Initializing trap framework (multi-core) on boot CPU %d...\n", cpuid);
        
        // 初始化向量表锁
        spinlock_init(&interrupt_table.lock, "interrupt_table");
        
        // 初始化共享中断池
        init_shared_interrupt_pool();
        
        // 初始化所有中断描述符
        for(int i = 0; i < MAX_INTERRUPTS; i++) {
            interrupt_table.entries[i].handler = default_interrupt_handler;
            interrupt_table.entries[i].name = "unregistered";
            interrupt_table.entries[i].priority = IRQ_PRIORITY_DISABLE;
            interrupt_table.entries[i].enabled = 0;
            interrupt_table.entries[i].count = 0;
            interrupt_table.entries[i].private_data = NULL;
            interrupt_table.entries[i].shared_list = NULL;
            interrupt_table.entries[i].is_shared = 0;
            interrupt_table.entries[i].shared_count = 0;
        }
        
        // 初始化嵌套控制
        interrupt_table.nested_level = 0;
        interrupt_table.nested_enabled = 0;
        interrupt_table.max_nested_level = MAX_INTERRUPT_NESTING;
        
        // 调用现有的trap系统初始化
        trap_kernel_init();
        
        printf("Trap framework initialized successfully (boot CPU %d)\n", cpuid);
    } else {
        printf("CPU %d: Waiting for trap framework initialization...\n", cpuid);
        // 等待CPU 0完成初始化
        while(interrupt_table.max_nested_level == 0) {
            for(volatile int i = 0; i < 100; i++);
        }
        printf("CPU %d: Trap framework ready\n", cpuid);
    }
}

// 注册中断处理函数
int register_interrupt(int irq, interrupt_handler_t handler)
{
    // 参数检查
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    if(handler == NULL) {
        return TRAP_ERR_NULL_HANDLER;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查是否已经注册
    if(interrupt_table.entries[irq].handler != default_interrupt_handler) {
        spinlock_release(&interrupt_table.lock);
        return TRAP_ERR_ALREADY_REG;
    }
    
    // 注册中断
    interrupt_table.entries[irq].handler = handler;
    interrupt_table.entries[irq].name = "registered";
    interrupt_table.entries[irq].priority = IRQ_PRIORITY_NORMAL;
    interrupt_table.entries[irq].enabled = 0;  // 注册后默认禁用
    interrupt_table.entries[irq].count = 0;
    interrupt_table.entries[irq].shared_list = NULL;
    interrupt_table.entries[irq].is_shared = 0;
    interrupt_table.entries[irq].shared_count = 1;     // 主处理函数算作1个
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Registered interrupt %d\n", irq);
    return TRAP_OK;
}

// 开启特定中断
int enable_interrupt(int irq)
{
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查是否已注册
    if(interrupt_table.entries[irq].handler == default_interrupt_handler) {
        spinlock_release(&interrupt_table.lock);
        return TRAP_ERR_NOT_REG;
    }
    
    interrupt_table.entries[irq].enabled = 1;
    
    // 根据中断类型进行特定的使能操作
    if(irq == IRQ_S_SOFT) {
        w_sie(r_sie() | SIE_SSIE);
    } else if(irq == IRQ_S_EXT) {
        w_sie(r_sie() | SIE_SEIE);
    }
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Enabled interrupt %d\n", irq);
    return TRAP_OK;
}

// 关闭特定中断
int disable_interrupt(int irq)
{
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    interrupt_table.entries[irq].enabled = 0;
    
    // 根据中断类型进行特定的禁用操作
    if(irq == IRQ_S_SOFT) {
        w_sie(r_sie() & ~SIE_SSIE);
    } else if(irq == IRQ_S_EXT) {
        w_sie(r_sie() & ~SIE_SEIE);
    }
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Disabled interrupt %d\n", irq);
    return TRAP_OK;
}

// ====== 扩展功能实现 ======
// 注销中断处理函数
int unregister_interrupt(int irq)
{
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查是否已注册
    if(interrupt_table.entries[irq].handler == default_interrupt_handler) {
        spinlock_release(&interrupt_table.lock);
        return TRAP_ERR_NOT_REG;
    }
    
    // 先清理共享链表
    shared_interrupt_node_t *current = interrupt_table.entries[irq].shared_list;
    while (current) {
        shared_interrupt_node_t *next = current->next;
        free_shared_node(current);
        current = next;
    }
    
    // 恢复默认状态
    interrupt_table.entries[irq].handler = default_interrupt_handler;
    interrupt_table.entries[irq].name = "unregistered";
    interrupt_table.entries[irq].priority = IRQ_PRIORITY_DISABLE;
    interrupt_table.entries[irq].enabled = 0;
    interrupt_table.entries[irq].shared_list = NULL;
    interrupt_table.entries[irq].is_shared = 0;
    interrupt_table.entries[irq].shared_count = 0;
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Unregistered interrupt %d\n", irq);
    return TRAP_OK;
}

// 设置中断优先级
int set_interrupt_priority(int irq, int priority)
{
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    if(priority < 0 || priority > IRQ_PRIORITY_HIGH) {
        printf("Invalid priority: %d\n", priority);
        return TRAP_ERR_INVALID_IRQ;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查是否已注册
    if(interrupt_table.entries[irq].handler == default_interrupt_handler) {
        spinlock_release(&interrupt_table.lock);
        return TRAP_ERR_NOT_REG;
    }
    
    int old_priority = interrupt_table.entries[irq].priority;
    interrupt_table.entries[irq].priority = priority;
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Changed interrupt %d priority: %d -> %d\n", irq, old_priority, priority);
    return TRAP_OK;
}

// ====== 中断嵌套控制 ======

// 启用中断嵌套
void enable_interrupt_nesting(void)
{
    spinlock_acquire(&interrupt_table.lock);
    interrupt_table.nested_enabled = 1;
    spinlock_release(&interrupt_table.lock);
    printf("Interrupt nesting enabled (max level: %d)\n", MAX_INTERRUPT_NESTING);
}

// 禁用中断嵌套
void disable_interrupt_nesting(void)
{
    spinlock_acquire(&interrupt_table.lock);
    interrupt_table.nested_enabled = 0;
    spinlock_release(&interrupt_table.lock);
    printf("Interrupt nesting disabled\n");
}

// ====== 中断处理函数 ======

// 嵌套感知的中断处理
void handle_interrupt(int irq)
{
    int cpuid = mycpuid();
    
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
#if DEBUG_INTERRUPT_ERROR
        printf("CPU %d: ERROR: Invalid interrupt number: %d\n", cpuid, irq);
#endif
        return;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查中断是否使能
    if(!interrupt_table.entries[irq].enabled) {
        spinlock_release(&interrupt_table.lock);
#if DEBUG_INTERRUPT_BASIC
        printf("WARNING: Received disabled interrupt: %d\n", irq);
#endif
        return;
    }

    // 更新CPU特定的统计
    per_cpu_interrupt_total[cpuid]++;
    
    // 检查嵌套限制
    if(interrupt_table.nested_enabled) {
        if(interrupt_table.nested_level >= interrupt_table.max_nested_level) {
            spinlock_release(&interrupt_table.lock);
#if DEBUG_INTERRUPT_ERROR
            printf("ERROR: Interrupt nesting limit reached, dropping interrupt %d\n", irq);
#endif
            return;
        }
        
        // 进入嵌套
        interrupt_table.nested_level++;
        per_cpu_interrupt_depth[cpuid]++;
        interrupt_table.entries[irq].count++;
        interrupt_handler_t handler = interrupt_table.entries[irq].handler;
        
        spinlock_release(&interrupt_table.lock);
        
        // 在嵌套模式下重新开启中断（允许更高优先级中断）
        intr_on();
        
#if DEBUG_INTERRUPT_VERBOSE
        printf("Entering nested interrupt %d (level %d)\n", irq, interrupt_table.nested_level);
#endif
        
        handler();
        
        // 退出嵌套，关闭中断
        intr_off();
        
        spinlock_acquire(&interrupt_table.lock);
        interrupt_table.nested_level--;
        per_cpu_interrupt_depth[cpuid]--;
#if DEBUG_INTERRUPT_VERBOSE
        printf("Exiting nested interrupt %d (level %d)\n", irq, interrupt_table.nested_level);
#endif
        spinlock_release(&interrupt_table.lock);
        
    } else {
        // 非嵌套模式，中断保持关闭
        interrupt_table.entries[irq].count++;
        interrupt_handler_t handler = interrupt_table.entries[irq].handler;
        spinlock_release(&interrupt_table.lock);
        
        handler();
    }
}

// 多核中断统计显示
void print_multicore_interrupt_stats(void)
{
    printf("\n=== Multi-Core Interrupt Statistics ===\n");
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 显示每个CPU的统计
    for (int cpu = 0; cpu < NCPU; cpu++) {
        printf("CPU %d: Total interrupts=%d, Current depth=%d\n", 
               cpu, per_cpu_interrupt_total[cpu], per_cpu_interrupt_depth[cpu]);
    }
    
    printf("\nGlobal interrupt statistics:\n");
    for(int i = 0; i < MAX_INTERRUPTS; i++) {
        if(interrupt_table.entries[i].count > 0 || 
           interrupt_table.entries[i].enabled) {
            printf("IRQ %d: %s, Count=%d, Enabled=%s, Priority=%d",
                   i,
                   interrupt_table.entries[i].name,
                   (int)interrupt_table.entries[i].count,
                   interrupt_table.entries[i].enabled ? "Yes" : "No",
                   interrupt_table.entries[i].priority);
            
            if (interrupt_table.entries[i].is_shared) {
                printf(", SHARED (%d handlers)", interrupt_table.entries[i].shared_count);
            }
            printf("\n");
        }
    }
    
    spinlock_release(&interrupt_table.lock);
    printf("=======================================\n\n");
}

// 优先级感知的中断处理
void handle_interrupt_with_priority(int irq)
{
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        return;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    if(!interrupt_table.entries[irq].enabled) {
        spinlock_release(&interrupt_table.lock);
        return;
    }
    
    int current_priority = interrupt_table.entries[irq].priority;
    
    // 检查是否有更高优先级的中断正在处理
    if(interrupt_table.nested_enabled && interrupt_table.nested_level > 0) {
        // 如果当前中断优先级不够高，延迟处理
        if(current_priority <= IRQ_PRIORITY_LOW) {
            spinlock_release(&interrupt_table.lock);
            printf("Interrupt %d deferred due to priority\n", irq);
            return;
        }
    }
    
    interrupt_table.entries[irq].count++;
    interrupt_handler_t handler = interrupt_table.entries[irq].handler;
    
    spinlock_release(&interrupt_table.lock);
    
    handler();
}

// 快速中断路径（减少锁竞争）
void fast_interrupt_handler(int irq)
{
    // 快速检查，避免获取锁
    if(irq < 0 || irq >= MAX_INTERRUPTS || 
       !interrupt_table.entries[irq].enabled) {
        return;
    }
    
    // 原子性地增加计数
    __sync_fetch_and_add(&interrupt_table.entries[irq].count, 1);
    
    // 直接调用处理函数，避免锁开销
    interrupt_table.entries[irq].handler();
}

// ====== 完整的共享中断支持 ======

// 注册共享中断 - 完整实现
int register_shared_interrupt(int irq, interrupt_handler_t handler, 
                             const char *name, int priority)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS || handler == NULL) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    if (!shared_pool_initialized) {
        init_shared_interrupt_pool();
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查是否已有主处理函数
    if (interrupt_table.entries[irq].handler == default_interrupt_handler) {
        // 第一个注册，作为主处理函数
        interrupt_table.entries[irq].handler = handler;
        interrupt_table.entries[irq].name = (char*)name;
        interrupt_table.entries[irq].priority = priority;
        interrupt_table.entries[irq].enabled = 0;
        interrupt_table.entries[irq].count = 0;
        interrupt_table.entries[irq].shared_list = NULL;
        interrupt_table.entries[irq].is_shared = 0;
        interrupt_table.entries[irq].shared_count = 1;
        
        spinlock_release(&interrupt_table.lock);
        printf("Registered primary interrupt %d: %s (priority: %d)\n", irq, name, priority);
        return TRAP_OK;
    }
    
    // 已有主处理函数，添加到共享链表
    shared_interrupt_node_t *new_node = alloc_shared_node();
    if (!new_node) {
        spinlock_release(&interrupt_table.lock);
        printf("Failed to allocate shared interrupt node for IRQ %d\n", irq);
        return TRAP_ERR_PLIC_FAIL;
    }
    
    // 配置新节点
    new_node->handler = handler;
    new_node->name = (char*)name;
    new_node->priority = priority;
    new_node->private_data = NULL;
    
    // 插入到链表头部
    new_node->next = interrupt_table.entries[irq].shared_list;
    interrupt_table.entries[irq].shared_list = new_node;
    interrupt_table.entries[irq].is_shared = 1;
    interrupt_table.entries[irq].shared_count++;
    
    // 更新主中断的优先级为最高优先级
    if (priority > interrupt_table.entries[irq].priority) {
        interrupt_table.entries[irq].priority = priority;
    }
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Registered shared interrupt %d: %s (priority: %d, total handlers: %d)\n", 
           irq, name, priority, interrupt_table.entries[irq].shared_count);
    return TRAP_OK;
}

// 注销共享中断
int unregister_shared_interrupt(int irq, interrupt_handler_t handler)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS || handler == NULL) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查是否为主处理函数
    if (interrupt_table.entries[irq].handler == handler) {
        // 如果有共享处理函数，提升第一个为主处理函数
        if (interrupt_table.entries[irq].shared_list) {
            shared_interrupt_node_t *first_shared = interrupt_table.entries[irq].shared_list;
            
            interrupt_table.entries[irq].handler = first_shared->handler;
            interrupt_table.entries[irq].name = first_shared->name;
            interrupt_table.entries[irq].priority = first_shared->priority;
            interrupt_table.entries[irq].shared_list = first_shared->next;
            interrupt_table.entries[irq].shared_count--;
            
            free_shared_node(first_shared);
            
            if (!interrupt_table.entries[irq].shared_list) {
                interrupt_table.entries[irq].is_shared = 0;
            }
            
            spinlock_release(&interrupt_table.lock);
            printf("Unregistered primary handler for IRQ %d, promoted shared handler\n", irq);
            return TRAP_OK;
        } else {
            // 没有其他处理函数，恢复默认
            interrupt_table.entries[irq].handler = default_interrupt_handler;
            interrupt_table.entries[irq].name = "unregistered";
            interrupt_table.entries[irq].priority = IRQ_PRIORITY_DISABLE;
            interrupt_table.entries[irq].enabled = 0;
            interrupt_table.entries[irq].is_shared = 0;
            interrupt_table.entries[irq].shared_count = 0;
            
            spinlock_release(&interrupt_table.lock);
            printf("Unregistered last handler for IRQ %d\n", irq);
            return TRAP_OK;
        }
    }
    
    // 在共享链表中查找并移除
    shared_interrupt_node_t **current = &interrupt_table.entries[irq].shared_list;
    
    while (*current) {
        if ((*current)->handler == handler) {
            shared_interrupt_node_t *to_remove = *current;
            *current = (*current)->next;
            interrupt_table.entries[irq].shared_count--;
            
            free_shared_node(to_remove);
            
            if (!interrupt_table.entries[irq].shared_list) {
                interrupt_table.entries[irq].is_shared = 0;
            }
            
            spinlock_release(&interrupt_table.lock);
            printf("Unregistered shared handler for IRQ %d\n", irq);
            return TRAP_OK;
        }
        current = &(*current)->next;
    }
    
    spinlock_release(&interrupt_table.lock);
    printf("Handler not found for IRQ %d\n", irq);
    return TRAP_ERR_NOT_REG;
}

// 处理共享中断 - 完整实现
void handle_shared_interrupt(int irq)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS) {
        printf("Invalid shared interrupt number: %d\n", irq);
        return;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    if (!interrupt_table.entries[irq].enabled) {
        spinlock_release(&interrupt_table.lock);
        printf("Received disabled shared interrupt: %d\n", irq);
        return;
    }
    
    // 增加总计数
    interrupt_table.entries[irq].count++;
    
    // 调用主处理函数
    interrupt_handler_t main_handler = interrupt_table.entries[irq].handler;
    
    // 获取共享链表的副本（避免长时间持锁）
    shared_interrupt_node_t *shared_list = interrupt_table.entries[irq].shared_list;
    int handler_count = interrupt_table.entries[irq].shared_count;
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Handling shared interrupt %d with %d handlers\n", irq, handler_count);
    
    // 调用主处理函数
    if (main_handler != default_interrupt_handler) {
        main_handler();
    }
    
    // 调用所有共享处理函数
    shared_interrupt_node_t *current = shared_list;
    int shared_called = 0;
    
    while (current) {
        if (current->handler) {
            current->handler();
            shared_called++;
        }
        current = current->next;
    }
    
    printf("Shared interrupt %d: called %d handlers\n", irq, shared_called + 1);
}

// ====== 统计和批处理 ======

// 批处理中断统计更新
void batch_update_interrupt_stats(void)
{
    static uint64 last_update_ticks = 0;
    
    uint64 current_ticks = timer_get_ticks();
    
    // 每100个tick批量更新一次统计
    if(current_ticks - last_update_ticks >= 100) {
        last_update_ticks = current_ticks;
        printf("Interrupt stats updated at tick %ld\n", current_ticks);
    }
}

// 获取中断次数
uint64 get_interrupt_count(int irq)
{
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        return 0;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    uint64 count = interrupt_table.entries[irq].count;
    spinlock_release(&interrupt_table.lock);
    
    return count;
}

// 打印中断统计（简化版本）
void print_interrupt_stats_simple(void)
{
    printf("\n=== Interrupt Statistics (Simple) ===\n");
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 显示每个CPU的统计
    int total_cpu_interrupts = 0;
    for (int cpu = 0; cpu < NCPU; cpu++) {
        if (per_cpu_interrupt_total[cpu] > 0) {
            printf("CPU %d: %d interrupts processed\n", cpu, per_cpu_interrupt_total[cpu]);
            total_cpu_interrupts += per_cpu_interrupt_total[cpu];
        }
    }
    
    if (total_cpu_interrupts > 0) {
        printf("Total CPU interrupts: %d\n", total_cpu_interrupts);
    }

    for(int i = 0; i < MAX_INTERRUPTS; i++) {
        if(interrupt_table.entries[i].count > 0 || 
           interrupt_table.entries[i].enabled) {
            printf("IRQ %d: %s, Count=%d, Enabled=%s, Priority=%d",
                   i,
                   interrupt_table.entries[i].name,
                   (int)interrupt_table.entries[i].count,
                   interrupt_table.entries[i].enabled ? "Yes" : "No",
                   interrupt_table.entries[i].priority);
            
            if (interrupt_table.entries[i].is_shared) {
                printf(", SHARED (%d handlers)", interrupt_table.entries[i].shared_count);
                
                // 显示共享处理函数列表
                shared_interrupt_node_t *current = interrupt_table.entries[i].shared_list;
                printf("\n    Shared handlers:");
                while (current) {
                    printf(" [%s:P%d]", current->name ? current->name : "unnamed", current->priority);
                    current = current->next;
                }
            }
            printf("\n");
        }
    }
    
    // 显示共享节点池使用情况
    int used_nodes = 0;
    for (int i = 0; i < MAX_SHARED_NODES; i++) {
        if (shared_node_pool[i].in_use) used_nodes++;
    }
    
    printf("Shared node pool: %d/%d used\n", used_nodes, MAX_SHARED_NODES);
    printf("Nesting: Level %d/%d (%s)\n",
           interrupt_table.nested_level,
           interrupt_table.max_nested_level,
           interrupt_table.nested_enabled ? "Enabled" : "Disabled");
    
    spinlock_release(&interrupt_table.lock);
    printf("=====================================\n\n");
}

// 批量注册中断
int register_interrupt_batch(interrupt_config_t *configs, int count)
{
    if (!configs || count <= 0) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    int success_count = 0;
    
    for (int i = 0; i < count; i++) {
        int ret;
        
        if (configs[i].flags & IRQ_FLAG_SHARED) {
            ret = register_shared_interrupt(configs[i].irq, configs[i].handler,
                                          configs[i].name, configs[i].priority);
        } else {
            ret = register_interrupt(configs[i].irq, configs[i].handler);
            if (ret == TRAP_OK) {
                set_interrupt_priority(configs[i].irq, configs[i].priority);
            }
        }
        
        if (ret == TRAP_OK) {
            success_count++;
        } else {
            printf("Failed to register interrupt %d in batch: %d\n", configs[i].irq, ret);
        }
    }
    
    printf("Batch registration: %d/%d successful\n", success_count, count);
    return success_count == count ? TRAP_OK : TRAP_ERR_PLIC_FAIL;
}

// ====== 实现策略函数 ======

// 基础时钟中断处理函数
void basic_timer_handler(void)
{
    // 更新系统时钟
    timer_update();
    
    // 清除软件中断标志
    w_sip(r_sip() & ~SIP_SSIP);
    
    // 简单的调试输出
    static int tick_count = 0;
    tick_count++;
    
    if(tick_count % 100 == 0) {  // 每100个tick输出一次
        printf("Timer: %d ticks, system ticks=%ld\n", tick_count, timer_get_ticks());
    }
}

// 设置基础时钟中断
void setup_basic_timer_interrupt(void)
{
    printf("Setting up basic timer interrupt...\n");
    
    // 注册时钟中断处理函数
    int ret = register_interrupt(IRQ_S_SOFT, basic_timer_handler);
    if(ret != TRAP_OK) {
        printf("Failed to register timer interrupt: %d\n", ret);
        return;
    }
    
    // 设置高优先级
    set_interrupt_priority(IRQ_S_SOFT, IRQ_PRIORITY_HIGH);
    
    // 启用时钟中断
    ret = enable_interrupt(IRQ_S_SOFT);
    if(ret != TRAP_OK) {
        printf("Failed to enable timer interrupt: %d\n", ret);
        return;
    }
    
    printf("Basic timer interrupt setup complete\n");
}

// UART中断处理函数
void uart_interrupt_handler(void)
{
    printf("UART interrupt received\n");
    
    // 处理UART数据
    while(1) {
        int c = uart_getc_sync();
        if(c == -1) break;
        
        printf("UART received: %c\n", c);
        
        // 简单回显
        uart_putc_sync(c);
    }
}

// 添加UART中断支持
void add_uart_interrupt_support(void)
{
    printf("Adding UART interrupt support...\n");
    
    // 注册UART中断处理函数（通过外部中断）
    int ret = register_interrupt(IRQ_S_EXT, uart_interrupt_handler);
    if(ret != TRAP_OK) {
        printf("Failed to register UART interrupt: %d\n", ret);
        return;
    }
    
    // 设置普通优先级
    set_interrupt_priority(IRQ_S_EXT, IRQ_PRIORITY_NORMAL);
    
    // 启用外部中断
    ret = enable_interrupt(IRQ_S_EXT);
    if(ret != TRAP_OK) {
        printf("Failed to enable UART interrupt: %d\n", ret);
        return;
    }
    
    printf("UART interrupt support added\n");
}

// 完整的中断系统初始化
void initialize_interrupt_system(void)
{
    // 阶段1：基础时钟中断
    printf("=== Phase 1: Basic Timer Interrupt ===\n");
    setup_basic_timer_interrupt();
    
    // 阶段2：添加其他中断源
    printf("=== Phase 2: Additional Interrupt Sources ===\n");
    add_uart_interrupt_support();
    
    // 阶段3：性能和扩展性配置
    printf("=== Phase 3: Performance and Scalability ===\n");
    
    // 配置中断嵌套（可选）
    enable_interrupt_nesting();
    
    printf("Interrupt system initialization complete\n");
    print_interrupt_stats_simple();
}

// ====== 高级功能和扩展 ======

// 获取共享中断数量
int get_shared_interrupt_count(int irq)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS) {
        return -1;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    int count = interrupt_table.entries[irq].shared_count;
    spinlock_release(&interrupt_table.lock);
    
    return count;
}

// 检查中断是否为共享中断
int is_shared_interrupt(int irq)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS) {
        return 0;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    int is_shared = interrupt_table.entries[irq].is_shared;
    spinlock_release(&interrupt_table.lock);
    
    return is_shared;
}

// 获取中断优先级
int get_interrupt_priority(int irq)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS) {
        return -1;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    int priority = interrupt_table.entries[irq].priority;
    spinlock_release(&interrupt_table.lock);
    
    return priority;
}

// 检查中断是否启用
int is_interrupt_enabled(int irq)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS) {
        return 0;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    int enabled = interrupt_table.entries[irq].enabled;
    spinlock_release(&interrupt_table.lock);
    
    return enabled;
}

// 重置中断统计
void reset_interrupt_stats(int irq)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS) {
        return;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    interrupt_table.entries[irq].count = 0;
    spinlock_release(&interrupt_table.lock);
    
    printf("Reset interrupt statistics for IRQ %d\n", irq);
}

// 重置所有中断统计
void reset_all_interrupt_stats(void)
{
    spinlock_acquire(&interrupt_table.lock);
    
    for (int i = 0; i < MAX_INTERRUPTS; i++) {
        interrupt_table.entries[i].count = 0;
    }
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Reset all interrupt statistics\n");
}

// 获取中断框架状态摘要
void get_interrupt_framework_status(void)
{
    printf("\n=== Interrupt Framework Status ===\n");
    
    spinlock_acquire(&interrupt_table.lock);
    
    int registered_count = 0;
    int enabled_count = 0;
    int shared_count = 0;
    uint64 total_interrupts = 0;
    
    for (int i = 0; i < MAX_INTERRUPTS; i++) {
        if (interrupt_table.entries[i].handler != default_interrupt_handler) {
            registered_count++;
        }
        if (interrupt_table.entries[i].enabled) {
            enabled_count++;
        }
        if (interrupt_table.entries[i].is_shared) {
            shared_count++;
        }
        total_interrupts += interrupt_table.entries[i].count;
    }
    
    // 计算共享节点池使用情况
    int used_nodes = 0;
    for (int i = 0; i < MAX_SHARED_NODES; i++) {
        if (shared_node_pool[i].in_use) {
            used_nodes++;
        }
    }
    
    printf("Registered interrupts: %d/%d\n", registered_count, MAX_INTERRUPTS);
    printf("Enabled interrupts: %d\n", enabled_count);
    printf("Shared interrupts: %d\n", shared_count);
    printf("Total interrupt count: %ld\n", total_interrupts);
    printf("Nesting enabled: %s\n", interrupt_table.nested_enabled ? "Yes" : "No");
    printf("Current nesting level: %d/%d\n", interrupt_table.nested_level, interrupt_table.max_nested_level);
    printf("Shared node pool usage: %d/%d (%.1f%%)\n", 
           used_nodes, MAX_SHARED_NODES, 
           (float)used_nodes / MAX_SHARED_NODES * 100.0);
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Framework status: OPERATIONAL\n");
    printf("===================================\n\n");
}

// ====== 性能和调试辅助函数 ======

// 中断性能测试
void test_interrupt_performance(int irq, int iterations)
{
    if (irq < 0 || irq >= MAX_INTERRUPTS || iterations <= 0) {
        printf("Invalid parameters for performance test\n");
        return;
    }
    
    printf("Starting interrupt performance test for IRQ %d (%d iterations)\n", irq, iterations);
    
    uint64 start_time = timer_get_ticks();
    uint64 start_count = get_interrupt_count(irq);
    
    for (int i = 0; i < iterations; i++) {
        handle_interrupt(irq);
    }
    
    uint64 end_time = timer_get_ticks();
    uint64 end_count = get_interrupt_count(irq);
    
    uint64 elapsed_time = end_time - start_time;
    uint64 processed_interrupts = end_count - start_count;
    
    printf("Performance test results:\n");
    printf("  Iterations: %d\n", iterations);
    printf("  Processed interrupts: %ld\n", processed_interrupts);
    printf("  Elapsed time: %ld cycles\n", elapsed_time);
    printf("  Average time per interrupt: %ld cycles\n", 
           processed_interrupts > 0 ? elapsed_time / processed_interrupts : 0);
    // 移除 timer_get_freq() 调用，简化性能统计
    if (elapsed_time > 0) {
        printf("  Interrupt processing rate: %ld interrupts per 1000 cycles\n", 
               processed_interrupts * 1000 / elapsed_time);
    }
}

// 中断框架完整性检查
void verify_interrupt_framework_integrity(void)
{
    printf("Verifying interrupt framework integrity...\n");
    
    int errors = 0;
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查基本数据结构
    if (interrupt_table.nested_level < 0 || 
        interrupt_table.nested_level > interrupt_table.max_nested_level) {
        printf("ERROR: Invalid nesting level: %d\n", interrupt_table.nested_level);
        errors++;
    }
    
    // 检查每个中断描述符
    for (int i = 0; i < MAX_INTERRUPTS; i++) {
        interrupt_desc_t *desc = &interrupt_table.entries[i];
        
        // 检查优先级范围
        if (desc->priority < IRQ_PRIORITY_DISABLE || desc->priority > IRQ_PRIORITY_HIGH) {
            printf("ERROR: Invalid priority for IRQ %d: %d\n", i, desc->priority);
            errors++;
        }
        
        // 检查共享中断一致性
        if (desc->is_shared && desc->shared_list == NULL && desc->shared_count > 1) {
            printf("ERROR: Inconsistent shared interrupt state for IRQ %d\n", i);
            errors++;
        }
        
        if (!desc->is_shared && desc->shared_list != NULL) {
            printf("ERROR: Non-shared interrupt has shared list for IRQ %d\n", i);
            errors++;
        }
    }
    
    // 检查共享节点池
    int allocated_nodes = 0;
    for (int i = 0; i < MAX_SHARED_NODES; i++) {
        if (shared_node_pool[i].in_use) {
            allocated_nodes++;
        }
    }
    
    // 计算实际使用的共享节点
    int actual_shared_nodes = 0;
    for (int i = 0; i < MAX_INTERRUPTS; i++) {
        if (interrupt_table.entries[i].shared_count > 1) {
            actual_shared_nodes += interrupt_table.entries[i].shared_count - 1;
        }
    }
    
    if (allocated_nodes != actual_shared_nodes) {
        printf("WARNING: Shared node count mismatch: allocated=%d, expected=%d\n", 
               allocated_nodes, actual_shared_nodes);
        // 这个可能不是错误，只是警告
    }
    
    spinlock_release(&interrupt_table.lock);
    
    if (errors == 0) {
        printf("Interrupt framework integrity check: PASSED\n");
    } else {
        printf("Interrupt framework integrity check: FAILED (%d errors)\n", errors);
    }
}

// 添加更详细的错误检查和报告
int register_interrupt_with_validation(int irq, interrupt_handler_t handler, const char *name)
{
    // 基本参数验证
    if (irq < 0 || irq >= MAX_INTERRUPTS) {
        printf("ERROR: Invalid IRQ %d (valid range: 0-%d)\n", irq, MAX_INTERRUPTS-1);
        return TRAP_ERR_INVALID_IRQ;
    }
    
    if (handler == NULL) {
        printf("ERROR: NULL handler for IRQ %d\n", irq);
        return TRAP_ERR_NULL_HANDLER;
    }
    
    if (name == NULL) {
        printf("WARNING: No name provided for IRQ %d\n", irq);
    }
    
    // 检查系统状态
    spinlock_acquire(&interrupt_table.lock);
    
    if (interrupt_table.entries[irq].handler != default_interrupt_handler) {
        spinlock_release(&interrupt_table.lock);
        printf("ERROR: IRQ %d already registered as '%s'\n", 
               irq, interrupt_table.entries[irq].name);
        return TRAP_ERR_ALREADY_REG;
    }
    
    // 执行注册
    interrupt_table.entries[irq].handler = handler;
    interrupt_table.entries[irq].name = (char*)(name ? name : "unnamed");
    interrupt_table.entries[irq].priority = IRQ_PRIORITY_NORMAL;
    interrupt_table.entries[irq].enabled = 0;
    interrupt_table.entries[irq].count = 0;
    interrupt_table.entries[irq].shared_list = NULL;
    interrupt_table.entries[irq].is_shared = 0;
    interrupt_table.entries[irq].shared_count = 1;
    
    spinlock_release(&interrupt_table.lock);
    
    printf("SUCCESS: Registered IRQ %d ('%s')\n", irq, name ? name : "unnamed");
    return TRAP_OK;
}

// 添加系统健康检查
void interrupt_system_health_check(void)
{
    printf("\n=== Interrupt System Health Check ===\n");
    
    spinlock_acquire(&interrupt_table.lock);
    
    int errors = 0;
    int warnings = 0;
    
    // 检查基本状态
    if (interrupt_table.nested_level < 0) {
        printf("ERROR: Negative nesting level: %d\n", interrupt_table.nested_level);
        errors++;
    }
    
    if (interrupt_table.nested_level > interrupt_table.max_nested_level) {
        printf("ERROR: Nesting level exceeds limit: %d > %d\n", 
               interrupt_table.nested_level, interrupt_table.max_nested_level);
        errors++;
    }
    
    // 检查中断描述符
    int active_interrupts = 0;
    for (int i = 0; i < MAX_INTERRUPTS; i++) {
        interrupt_desc_t *desc = &interrupt_table.entries[i];
        
        if (desc->handler != default_interrupt_handler) {
            active_interrupts++;
            
            // 检查优先级
            if (desc->priority < IRQ_PRIORITY_DISABLE || desc->priority > IRQ_PRIORITY_HIGH) {
                printf("ERROR: Invalid priority for IRQ %d: %d\n", i, desc->priority);
                errors++;
            }
            
            // 检查共享中断一致性
            if (desc->is_shared && desc->shared_count <= 1) {
                printf("WARNING: IRQ %d marked as shared but count is %d\n", 
                       i, desc->shared_count);
                warnings++;
            }
            
            if (!desc->is_shared && desc->shared_list != NULL) {
                printf("ERROR: IRQ %d not shared but has shared list\n", i);
                errors++;
            }
        }
    }
    
    // 检查共享节点池
    int pool_used = 0;
    for (int i = 0; i < MAX_SHARED_NODES; i++) {
        if (shared_node_pool[i].in_use) {
            pool_used++;
        }
    }
    
    printf("Active interrupts: %d\n", active_interrupts);
    printf("Shared pool usage: %d/%d nodes\n", pool_used, MAX_SHARED_NODES);
    printf("Nesting level: %d/%d\n", interrupt_table.nested_level, interrupt_table.max_nested_level);
    printf("Health check result: %d errors, %d warnings\n", errors, warnings);
    
    if (errors == 0 && warnings == 0) {
        printf("System status: HEALTHY\n");
    } else if (errors == 0) {
        printf("System status: STABLE (with warnings)\n");
    } else {
        printf("System status: CRITICAL (errors detected)\n");
    }
    
    spinlock_release(&interrupt_table.lock);
    printf("=====================================\n\n");
}