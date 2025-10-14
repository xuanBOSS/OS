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

// 全局中断向量表
interrupt_vector_table_t interrupt_table;

// 默认的空中断处理函数
static void default_interrupt_handler(void)
{
    printf("Warning: unhandled interrupt occurred\n");
}

// ====== 核心接口实现 ======

// 初始化中断系统
void trap_init(void)
{
    printf("Initializing trap framework...\n");
    
    // 初始化向量表锁
    spinlock_init(&interrupt_table.lock, "interrupt_table");
    
    // 初始化所有中断描述符
    for(int i = 0; i < MAX_INTERRUPTS; i++) {
        interrupt_table.entries[i].handler = default_interrupt_handler;
        interrupt_table.entries[i].name = "unregistered";
        interrupt_table.entries[i].priority = IRQ_PRIORITY_DISABLE;
        interrupt_table.entries[i].enabled = 0;
        interrupt_table.entries[i].count = 0;
        interrupt_table.entries[i].private_data = NULL;
        interrupt_table.entries[i].next = NULL;
    }
    
    // 初始化嵌套控制
    interrupt_table.nested_level = 0;
    interrupt_table.nested_enabled = 0;  // 默认不允许嵌套
    interrupt_table.max_nested_level = MAX_INTERRUPT_NESTING;
    
    // 调用现有的trap系统初始化
    trap_kernel_init();
    
    printf("Trap framework initialized successfully\n");
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
    interrupt_table.entries[irq].next = NULL;
    
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

// 每个CPU核心初始化
void trap_inithart(void)
{
    trap_kernel_inithart();
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
    if(irq < 0 || irq >= MAX_INTERRUPTS) {
        printf("Invalid interrupt number: %d\n", irq);
        return;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 检查中断是否使能
    if(!interrupt_table.entries[irq].enabled) {
        spinlock_release(&interrupt_table.lock);
        printf("Received disabled interrupt: %d\n", irq);
        return;
    }
    
    // 检查嵌套限制
    if(interrupt_table.nested_enabled) {
        if(interrupt_table.nested_level >= interrupt_table.max_nested_level) {
            spinlock_release(&interrupt_table.lock);
            printf("Interrupt nesting limit reached, dropping interrupt %d\n", irq);
            return;
        }
        
        // 进入嵌套
        interrupt_table.nested_level++;
        interrupt_table.entries[irq].count++;
        interrupt_handler_t handler = interrupt_table.entries[irq].handler;
        
        spinlock_release(&interrupt_table.lock);
        
        // 在嵌套模式下重新开启中断（允许更高优先级中断）
        intr_on();
        printf("Entering nested interrupt %d (level %d)\n", irq, interrupt_table.nested_level);
        
        handler();
        
        // 退出嵌套，关闭中断
        intr_off();
        
        spinlock_acquire(&interrupt_table.lock);
        interrupt_table.nested_level--;
        printf("Exiting nested interrupt %d (level %d)\n", irq, interrupt_table.nested_level);
        spinlock_release(&interrupt_table.lock);
        
    } else {
        // 非嵌套模式，中断保持关闭
        interrupt_table.entries[irq].count++;
        interrupt_handler_t handler = interrupt_table.entries[irq].handler;
        spinlock_release(&interrupt_table.lock);
        
        handler();
    }
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

// ====== 共享中断支持 ======

// 注册共享中断
int register_shared_interrupt(int irq, interrupt_handler_t handler, 
                             const char *name, int priority)
{
    if(irq < 0 || irq >= MAX_INTERRUPTS || handler == NULL) {
        return TRAP_ERR_INVALID_IRQ;
    }
    
    spinlock_acquire(&interrupt_table.lock);
    
    // 如果中断已被注册，添加到链表中
    if(interrupt_table.entries[irq].handler != default_interrupt_handler) {
        // 分配新的描述符节点（这里简化，实际需要内存管理）
        printf("Warning: Shared interrupt allocation not fully implemented\n");
        spinlock_release(&interrupt_table.lock);
        return TRAP_ERR_PLIC_FAIL;
    }
    
    // 否则正常注册
    interrupt_table.entries[irq].handler = handler;
    interrupt_table.entries[irq].name = (char*)name;
    interrupt_table.entries[irq].priority = priority;
    interrupt_table.entries[irq].enabled = 1;
    interrupt_table.entries[irq].count = 0;
    interrupt_table.entries[irq].next = NULL;
    
    spinlock_release(&interrupt_table.lock);
    
    printf("Registered interrupt %d: %s\n", irq, name);
    return TRAP_OK;
}

// 处理共享中断
void handle_shared_interrupt(int irq)
{
    // 简化版本，直接调用主处理函数
    handle_interrupt(irq);
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

// 打印中断统计
void print_interrupt_stats(void)
{
    printf("\n=== Interrupt Statistics ===\n");
    printf("IRQ | Name         | Count    | Enabled | Priority | Nested Level\n");
    printf("----+-------------+----------+---------+----------+-------------\n");
    
    spinlock_acquire(&interrupt_table.lock);
    
    for(int i = 0; i < MAX_INTERRUPTS; i++) {
        if(interrupt_table.entries[i].count > 0 || 
           interrupt_table.entries[i].enabled) {
            printf("%3d | %-11s | %8ld | %7s | %8d | %11d\n",
                   i,
                   interrupt_table.entries[i].name,
                   interrupt_table.entries[i].count,
                   interrupt_table.entries[i].enabled ? "Yes" : "No",
                   interrupt_table.entries[i].priority,
                   interrupt_table.nested_level);
        }
    }
    
    printf("Nesting: %s (Level: %d/%d)\n",
           interrupt_table.nested_enabled ? "Enabled" : "Disabled",
           interrupt_table.nested_level,
           interrupt_table.max_nested_level);
    
    spinlock_release(&interrupt_table.lock);
    printf("============================\n\n");
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
    print_interrupt_stats();
}

