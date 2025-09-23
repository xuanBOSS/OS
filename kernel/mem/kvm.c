#include "mem/kvm.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "lib/print.h"
#include "riscv.h"
#include "proc/proc.h"

// 全局内核页表
pagetable_t kernel_pagetable;

// 外部符号（来自kernel.ld）
extern char etext[];

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
            printf("kvm_map failed at offset %lx\n", offset);
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
    
    // 减少映射范围，避免内存耗尽
    // 只映射必要的设备和内核区域
    kvm_map(UART0, UART0, PGSIZE, PTE_R | PTE_W);
    
    // 映射较小的PLIC区域
    kvm_map(PLIC, PLIC, 0x100000, PTE_R | PTE_W);  // 1MB而不是4MB
    
    // 映射内核代码段
    uint64 kernel_text_size = PGROUNDUP(32 * PGSIZE);  // 限制为128KB
    kvm_map(KERNBASE, KERNBASE, kernel_text_size, PTE_R | PTE_X);
    
    // 映射内核数据段（限制大小）
    uint64 data_start = KERNBASE + kernel_text_size;
    uint64 data_size = 64 * PGSIZE;  // 限制为256KB
    kvm_map(data_start, data_start, data_size, PTE_R | PTE_W);
    
    printf("Kernel page table created successfully!\n");
    printf("=======================================\n");
}

/**
 * 在当前CPU上启用内核页表
 */
void kvm_inithart(void)
{
    int cpuid = mycpuid();
    
    printf("CPU%d: Enabling kernel page table...\n", cpuid);
    
    // 1. 内存屏障，确保页表写入完成
    sfence_vma();
    
    // 2. 设置SATP寄存器，启用Sv39页表
    w_satp(MAKE_SATP(kernel_pagetable));
    
    // 3. 刷新TLB
    sfence_vma();
    
    printf("CPU%d: Virtual memory enabled!\n", cpuid);
}