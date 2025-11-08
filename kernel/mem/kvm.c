#include "mem/kvm.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "lib/print.h"
#include "riscv.h"
#include "proc/proc.h"
#include "memlayout.h"

// 全局内核页表
pagetable_t kernel_pagetable;

// 外部符号（来自kernel.ld）
extern char etext[];
extern char trampoline[];

/**
 * 内核页表映射辅助函数
 * @param va 虚拟地址
 * @param pa 物理地址  
 * @param size 映射大小
 * @param perm 权限
 */
static void kvm_map(uint64 va, uint64 pa, uint64 size, int perm)
{
    // 移除过多的调试输出
    // 确保地址页对齐
    if (va % PGSIZE != 0 || pa % PGSIZE != 0 || size % PGSIZE != 0) {
        panic("kvm_map: addresses not page-aligned");
    }
    
    // 逐页建立映射
    for (uint64 offset = 0; offset < size; offset += PGSIZE) {
        if (map_page(kernel_pagetable, va + offset, pa + offset, perm) != 0) {
            panic("kvm_map: map_page failed");
        }
    }
}

/**
 * 初始化内核页表
 * 建立内核所需的所有映射
 */
void kvm_init(void)
{
    printf("=== Kernel Virtual Memory Initialization ===\n");
    
    kernel_pagetable = create_pagetable();
    if (!kernel_pagetable) {
        panic("kvm_init: failed to create kernel page table");
    }
    
    // 设备映射
    kvm_map(UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);
    kvm_map(PLIC_BASE, PLIC_BASE, 0x100000, PTE_R | PTE_W);
    
    // ✅ 关键修复：映射整个物理内存范围，确保内核可以访问所有页面
    uint64 phys_start = KERNBASE;     // 0x80000000
    uint64 phys_end = PHYSTOP;        // 0x88000000
    uint64 phys_size = phys_end - phys_start;
    
    printf("Mapping entire physical memory: 0x%lx - 0x%lx (size: %ld MB)\n", 
           phys_start, phys_end, phys_size / (1024*1024));
    
    // 映射整个物理内存区域（恒等映射）
    kvm_map(phys_start, phys_start, phys_size, PTE_R | PTE_W | PTE_X);
    
    // trampoline 映射
    extern char trampoline[];
    kvm_map(TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    printf("Trampoline mapped: VA=0x%lx -> PA=0x%lx\n", TRAMPOLINE, (uint64)trampoline);

    printf("Kernel page table created successfully!\n");
    printf("=======================================\n");
}

/**
 * 在当前CPU上启用内核页表
 */
void kvm_inithart(void)
{
    printf("About to enable kernel page table...\n");
    
    // 验证页表内容
    extern pagetable_t kernel_pagetable;
    printf("Kernel pagetable address: 0x%lx\n", (uint64)kernel_pagetable);
    
    // 1. 内存屏障
    sfence_vma();
    
    // 2. 计算并显示 SATP 值
    uint64 satp_value = MAKE_SATP(kernel_pagetable);
    printf("Setting SATP to: 0x%lx\n", satp_value);
    
    // 3. 设置SATP寄存器
    w_satp(satp_value);
    
    // ✅ 关键：添加测试指令确保页表工作
    printf("Testing page table access...\n");
    
    // 4. 刷新TLB
    sfence_vma();
    
    // 5. 测试访问
    volatile int test_var = 42;
    printf("Test variable value: %d\n", test_var);
    
    printf("Kernel page table enabled successfully!\n");
}
