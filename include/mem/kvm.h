#ifndef __KVM_H__
#define __KVM_H__

#include "common.h"
#include "mem/vmem.h"

// 内核虚拟地址布局
#define KERNBASE     0x80000000UL    // 内核基地址
#define PHYSTOP      0x88000000UL    // 物理内存结束地址

// 设备地址（恒等映射）
#define UART0        0x10000000UL
#define PLIC         0x0c000000UL

// 全局内核页表
extern pagetable_t kernel_pagetable;

// 内核虚拟内存管理接口
void kvm_init(void);       // 初始化内核页表
void kvm_inithart(void);   // 在CPU上启用内核页表

#endif