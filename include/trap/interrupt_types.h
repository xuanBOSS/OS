//定义中断类型、编号和优先级常量

#ifndef __INTERRUPT_TYPES_H__
#define __INTERRUPT_TYPES_H__

// RISC-V 标准中断编号
#define IRQ_S_SOFT    1    // S-mode 软件中断
#define IRQ_M_SOFT    3    // M-mode 软件中断  
#define IRQ_S_TIMER   5    // S-mode 定时器中断
#define IRQ_M_TIMER   7    // M-mode 定时器中断
#define IRQ_S_EXT     9    // S-mode 外部中断
#define IRQ_M_EXT     11   // M-mode 外部中断

// 外设中断编号 (通过PLIC路由)
#define IRQ_UART      10   // UART中断
#define IRQ_DISK      1    // 磁盘中断
#define IRQ_KEYBOARD  33   // 键盘中断 
#define IRQ_NETWORK   34   // 网络中断

// 最大中断数量
#define MAX_INTERRUPTS 64

// 中断优先级定义
#define IRQ_PRIORITY_HIGH    3
#define IRQ_PRIORITY_NORMAL  2  
#define IRQ_PRIORITY_LOW     1
#define IRQ_PRIORITY_DISABLE 0

#endif
