/* memory layout */
#ifndef __MEMLAYOUT_H__
#define __MEMLAYOUT_H__

// 基本页面大小
#define PGSIZE 4096

// 内核基地址
#define KERNEL_BASE 0x80000000ul

// UART 相关
#define UART_BASE  0x10000000ul
#define UART_IRQ   10

// platform-level interrupt controller(PLIC)
#define PLIC_BASE 0x0c000000ul
#define PLIC_PRIORITY(id) (PLIC_BASE + (id) * 4)
#define PLIC_PENDING (PLIC_BASE + 0x1000)
#define PLIC_MENABLE(hart) (PLIC_BASE + 0x2000 + (hart)*0x100)
#define PLIC_SENABLE(hart) (PLIC_BASE + 0x2080 + (hart)*0x100)
#define PLIC_MPRIORITY(hart) (PLIC_BASE + 0x200000 + (hart)*0x2000)
#define PLIC_SPRIORITY(hart) (PLIC_BASE + 0x201000 + (hart)*0x2000)
#define PLIC_MCLAIM(hart) (PLIC_BASE + 0x200004 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC_BASE + 0x201004 + (hart)*0x2000)

// core local interruptor(CLINT)
#define CLINT_BASE 0x2000000ul
#define CLINT_MSIP(hartid) (CLINT_BASE + 4 * (hartid))
#define CLINT_MTIMECMP(hartid) (CLINT_BASE + 0x4000 + 8 * (hartid))
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)

// === Lab4 用户态虚拟地址布局 ===
// 用户态虚拟地址空间布局（Sv39）
#define USER_VA_MAX     (1UL << 38)         // 256GB用户地址空间

// 用户态特殊页面（高地址区域）
#define TRAMPOLINE_VA   (USER_VA_MAX - PGSIZE)           // 0x3FFFFFF000
#define TRAPFRAME_VA    (TRAMPOLINE_VA - PGSIZE)         // 0x3FFFFFE000

// 添加别名定义
#define TRAMPOLINE      TRAMPOLINE_VA
#define TRAPFRAME       TRAPFRAME_VA

// 用户态程序布局（低地址区域）
#define USER_TEXT_BASE  0x1000              // 用户代码起始地址（避开NULL页）
#define USER_DATA_BASE  0x2000              // 用户数据起始地址
#define USER_HEAP_BASE  0x3000              // 用户堆起始地址
#define USER_STACK_TOP  0x4000              // 用户栈顶（16KB处，适合小程序）

// 内核虚拟地址布局
#define KERNBASE        0x80000000UL        // 内核基地址
#define PHYSTOP         0x88000000UL        // 物理内存结束地址

// 设备映射地址（内核虚拟地址空间）
#define UART_VA         UART_BASE           // UART虚拟地址（恒等映射）
#define PLIC_VA         PLIC_BASE           // PLIC虚拟地址（恒等映射）
#define CLINT_VA        CLINT_BASE          // CLINT虚拟地址（恒等映射）

// 页表相关定义
#define SATP_SV39 (8L << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// 内核栈虚拟地址计算（每个进程有独立的内核栈）
#define KSTACK(p)       (TRAMPOLINE_VA - 3*PGSIZE - (p)*2*PGSIZE)

// ✅ 重要：添加来自 kernel.ld 的外部符号声明
extern char etext[];        // 内核代码段结束地址
extern char trampoline[];   // trampoline 页地址
extern char KERNEL_DATA[];  // 内核数据段起始地址
extern char ALLOC_BEGIN[];  // 可分配内存起始地址  
extern char ALLOC_END[];    // 可分配内存结束地址

#endif
