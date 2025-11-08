#ifndef __VMEM_H__
#define __VMEM_H__

#include "common.h"

/*
    我们使用RISC-V体系结构中的SV39作为虚拟内存的设计规范

    satp寄存器: MODE(4) + ASID(16) + PPN(44)
    MODE控制虚拟内存模式 ASID与Flash刷新有关 PPN存放页表基地址

    基础页面 4KB
    
    VA和PA的构成:
    VA: VPN[2] + VPN[1] + VPN[0] + offset    9 + 9 + 9 + 12 = 39 (使用uint64存储) => 最大虚拟地址为512GB 
    PA: PPN[2] + PPN[1] + PPN[0] + offset   26 + 9 + 9 + 12 = 56 (使用uint64存储)
    
    为什么是 "9" : 4KB / uint64 = 512 = 2^9 所以一个物理页可以存放512个页表项
    我们使用三级页表对应三级VPN, VPN[2]称为顶级页表、VPN[1]称为次级页表、VPN[0]称为低级页表

    PTE定义:
    reserved + PPN[2] + PPN[1] + PPN[0] + RSW + D A G U X W R V  共64bit
       10        26       9        9       2    1 1 1 1 1 1 1 1
    
    需要关注的部分:
    V : valid
    X W R : execute write read (全0意味着这是页表所在的物理页)
    U : 用户态是否可以访问
    PPN区域 : 存放物理页号

*/

// 页表类型定义
typedef uint64 pte_t;
typedef pte_t* pagetable_t;

// SATP寄存器操作
#define SATP_SV39 (8L << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))

// 虚拟地址解析宏
#define VPN_SHIFT(level) (12 + 9 * (level))
#define VPN_MASK(va, level) (((va) >> VPN_SHIFT(level)) & 0x1FF)

// 物理地址和PTE转换
#define PA_TO_PTE(pa)  ((((uint64)(pa)) >> 12) << 10)
#define PTE_TO_PA(pte) (((pte) >> 10) << 12)

// PTE标志位定义
#define PTE_V (1L << 0)  // valid
#define PTE_R (1L << 1)  // read
#define PTE_W (1L << 2)  // write
#define PTE_X (1L << 3)  // execute
#define PTE_U (1L << 4)  // user
#define PTE_G (1L << 5)  // global
#define PTE_A (1L << 6)  // accessed
#define PTE_D (1L << 7)  // dirty

#define PTE_FLAGS(pte) ((pte) & 0x3FF)
#define VA_MAX (1UL << 38)

// 检查PTE是否为中间页表项
#define PTE_IS_TABLE(pte) (((pte) & PTE_V) && (((pte) & (PTE_R | PTE_W | PTE_X)) == 0))

// 核心接口
pagetable_t create_pagetable(void);
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm);
void destroy_pagetable(pagetable_t pt);

// 辅助函数
pte_t* walk_create(pagetable_t pt, uint64 va);
pte_t* walk_lookup(pagetable_t pt, uint64 va);

// 调试函数
void dump_pagetable(pagetable_t pt, int level);
void vm_print(pagetable_t pt);

#endif
