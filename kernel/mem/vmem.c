#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/str.h"
#include "lib/print.h"

extern pagetable_t kernel_pagetable;
/** 
 *创建空的页表
 *@return 新的页表，失败返回NULL
 */ 
pagetable_t create_pagetable(void)
{
    pagetable_t pt = (pagetable_t)pmem_alloc(true);
    if (pt) {
        memset(pt, 0, PGSIZE);
    }
    return pt;
}

/** 
 *页表遍历 - 创建模式
 *逐级遍历页表，必要时创建中间级页表
 *@param pt 根页表
 *@param va 虚拟地址
 *@return PTE指针，失败返回NULL
 */
pte_t* walk_create(pagetable_t pt, uint64 va)
{
    if (va >= VA_MAX) {
        return NULL;
    }
    
    // 从Level-2遍历到Level-0
    for (int level = 2; level > 0; level--) {
        uint64 index = VPN_MASK(va, level);
        pte_t* pte = &pt[index];
        
        if (*pte & PTE_V) {
            // 页表项有效，获取下一级页表
            pt = (pagetable_t)PTE_TO_PA(*pte);
        } else {
            // 页表项无效，创建新的页表页
            pagetable_t new_pt = create_pagetable();
            if (!new_pt) {
                return NULL;
            }
            
            // 设置PTE指向新页表
            *pte = PA_TO_PTE((uint64)new_pt) | PTE_V;
            pt = new_pt;
        }
    }
    
    // 返回Level-0的PTE地址
    uint64 index = VPN_MASK(va, 0);
    return &pt[index];
}

/**
 * 页表遍历 - 查询模式
 * 只查询，不创建新页表
 * @param pt 根页表
 * @param va 虚拟地址
 * @return PTE指针，未找到返回NULL
 */
pte_t* walk_lookup(pagetable_t pt, uint64 va)
{
    if (va >= VA_MAX) {
        return NULL;
    }
    
    for (int level = 2; level > 0; level--) {
        uint64 index = VPN_MASK(va, level);
        pte_t* pte = &pt[index];
        
        if (*pte & PTE_V) {
            pt = (pagetable_t)PTE_TO_PA(*pte);
        } else {
            return NULL;
        }
    }
    
    uint64 index = VPN_MASK(va, 0);
    pte_t* pte = &pt[index];
    
    return (*pte & PTE_V) ? pte : NULL;
}

/**
 * 建立页面映射
 * @param pt 页表
 * @param va 虚拟地址（必须页对齐）
 * @param pa 物理地址（必须页对齐）
 * @param perm 权限标志
 * @return 成功返回0，失败返回-1
 */
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm)
{
    // 地址对齐检查
    if (va % PGSIZE != 0 || pa % PGSIZE != 0) {
        return -1;
    }
    
    // 获取PTE地址
    pte_t* pte = walk_create(pt, va);
    if (!pte) {
        return -1;
    }
    
    // 检查是否已经映射
    if (*pte & PTE_V) {
        return -1;  // 已映射，返回错误
    }
    
    // 建立映射
    *pte = PA_TO_PTE(pa) | perm | PTE_V;
    return 0;
}

/**
 * ✅ 新增：取消页面映射
 * @param pt 页表
 * @param va 虚拟地址
 */
void unmap_page(pagetable_t pt, uint64 va)
{
    if (!pt) {
        printf("unmap_page: null page table\n");
        return;
    }
    
    if (va % PGSIZE != 0) {
        printf("unmap_page: address not page-aligned\n");
        return;
    }
    
    pte_t* pte = walk_lookup(pt, va);
    if (pte && (*pte & PTE_V)) {
        *pte = 0;  // 清除PTE
        printf("unmap_page: unmapped va=0x%lx\n", va);
    } else {
        printf("unmap_page: page at 0x%lx not mapped\n", va);
    }
}

/**
 * ✅ 新增：虚拟地址到物理地址转换
 * @param pt 页表
 * @param va 虚拟地址
 * @return 物理地址，失败返回0
 */
uint64 va_to_pa(pagetable_t pt, uint64 va)
{
    if (!pt) {
        return 0;
    }
    
    pte_t* pte = walk_lookup(pt, va);
    if (!pte || !(*pte & PTE_V)) {
        return 0;
    }
    
    uint64 pa = PTE_TO_PA(*pte);
    uint64 offset = va & (PGSIZE - 1);
    
    return pa + offset;
}

/**
 * 递归销毁页表
 * @param pt 要销毁的页表
 */
void destroy_pagetable(pagetable_t pt)
{
    if (!pt) return;
    
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        
        if ((pte & PTE_V) && PTE_IS_TABLE(pte)) {
            pagetable_t child_pt = (pagetable_t)PTE_TO_PA(pte);
            destroy_pagetable(child_pt);
        }
    }
    
    pmem_free((uint64)pt, true);
}

/**
 * 调试：打印页表结构
 * @param pt 页表
 * @param level 当前级别
 */
void dump_pagetable(pagetable_t pt, int level)
{
    printf("Page table at level %d: %p\n", level, pt);
}

void vm_print(pagetable_t pt)
{
    printf("Virtual memory layout: %p\n", pt);
}

// ✅ 新增：老师代码兼容函数
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    // ✅ 如果传入 NULL，自动使用内核页表
    if (pgtbl == NULL) {
        pgtbl = kernel_pagetable;
    }
    
    if (alloc) {
        return walk_create(pgtbl, va);
    } else {
        return walk_lookup(pgtbl, va);
    }
}

void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    uint64 end_va = va + len;
    
    for (uint64 curr_va = va; curr_va < end_va; curr_va += PGSIZE, pa += PGSIZE) {
        if (map_page(pgtbl, curr_va, pa, perm) != 0) {
            printf("vm_mappages: failed to map page at 0x%lx\n", curr_va);
            return;
        }
    }
}

void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    uint64 end_va = va + len;
    
    for (uint64 curr_va = va; curr_va < end_va; curr_va += PGSIZE) {
        if (freeit) {
            uint64 pa = va_to_pa(pgtbl, curr_va);
            if (pa != 0) {
                pmem_free(pa, false);  // 假设是用户页面
            }
        }
        unmap_page(pgtbl, curr_va);
    }
}
