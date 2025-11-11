#include "common.h"
#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "lib/print.h"
#include "mem/str.h"
#include "memlayout.h"

// ================================
// 调试和显示函数
// ================================

void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL) {
        printf("NULL\n");
        return;
    }
    
    int count = 0;
    while(tmp != NULL) {
        printf("[%d] allocable region: 0x%lx ~ 0x%lx (%d pages, %ld KB)\n", 
               count++, 
               (uint64)tmp->begin, 
               (uint64)tmp->begin + tmp->npages * PGSIZE,
               tmp->npages,
               (tmp->npages * PGSIZE) / 1024);
        tmp = tmp->next;
    }
    printf("Total mmap regions: %d\n", count);
}

// ================================
// 页表管理函数
// ================================

void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    if (!pgtbl) {
        printf("uvm_destroy_pgtbl: null page table\n");
        return;
    }
    
    printf("uvm_destroy_pgtbl: destroying page table at 0x%lx\n", (uint64)pgtbl);
    
    // 使用现有的 destroy_pagetable 函数
    destroy_pagetable(pgtbl);
    
    printf("uvm_destroy_pgtbl: page table destroyed\n");
}

void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    if (!old || !new) {
        printf("uvm_copy_pgtbl: invalid page table pointers\n");
        return;
    }
    
    printf("uvm_copy_pgtbl: copying page table\n");
    printf("  old pgtbl: 0x%lx\n", (uint64)old);
    printf("  new pgtbl: 0x%lx\n", (uint64)new);
    printf("  heap_top: 0x%lx\n", heap_top);
    printf("  ustack_pages: %d\n", ustack_pages);
    
    // 1. 复制用户堆区域
    if (heap_top > USER_HEAP_BASE) {
        uint64 heap_size = heap_top - USER_HEAP_BASE;
        uint32 heap_pages = (heap_size + PGSIZE - 1) / PGSIZE;
        
        printf("uvm_copy_pgtbl: copying heap (%d pages)\n", heap_pages);
        
        for (uint32 i = 0; i < heap_pages; i++) {
            uint64 va = USER_HEAP_BASE + i * PGSIZE;
            uint64 old_pa = va_to_pa(old, va);
            
            if (old_pa != 0) {
                // 分配新的物理页
                uint64 new_pa = (uint64)pmem_alloc(false);
                if (new_pa) {
                    // 复制页面内容
                    memcpy((void*)new_pa, (void*)old_pa, PGSIZE);
                    
                    // 映射到新页表
                    if (map_page(new, va, new_pa, PTE_R | PTE_W | PTE_U) != 0) {
                        printf("uvm_copy_pgtbl: failed to map heap page at 0x%lx\n", va);
                        pmem_free(new_pa, false);  // ✅ 修复：添加 in_kernel 参数
                    }
                } else {
                    printf("uvm_copy_pgtbl: failed to allocate page for heap\n");
                }
            }
        }
    }
    
    // 2. 复制用户栈区域
    if (ustack_pages > 0) {
        printf("uvm_copy_pgtbl: copying stack (%d pages)\n", ustack_pages);
        
        for (uint32 i = 0; i < ustack_pages; i++) {
            uint64 va = USER_STACK_TOP - (i + 1) * PGSIZE;
            uint64 old_pa = va_to_pa(old, va);
            
            if (old_pa != 0) {
                // 分配新的物理页
                uint64 new_pa = (uint64)pmem_alloc(false);
                if (new_pa) {
                    // 复制页面内容
                    memcpy((void*)new_pa, (void*)old_pa, PGSIZE);
                    
                    // 映射到新页表
                    if (map_page(new, va, new_pa, PTE_R | PTE_W | PTE_U) != 0) {
                        printf("uvm_copy_pgtbl: failed to map stack page at 0x%lx\n", va);
                        pmem_free(new_pa, false);  // ✅ 修复：添加 in_kernel 参数
                    }
                } else {
                    printf("uvm_copy_pgtbl: failed to allocate page for stack\n");
                }
            }
        }
    }
    
    // 3. 复制 mmap 区域
    mmap_region_t* region = mmap;
    while (region) {
        printf("uvm_copy_pgtbl: copying mmap region 0x%lx (%d pages)\n", 
               (uint64)region->begin, region->npages);
        
        for (uint32 i = 0; i < region->npages; i++) {
            uint64 va = (uint64)region->begin + i * PGSIZE;
            uint64 old_pa = va_to_pa(old, va);
            
            if (old_pa != 0) {
                // 分配新的物理页
                uint64 new_pa = (uint64)pmem_alloc(false);
                if (new_pa) {
                    // 复制页面内容
                    memcpy((void*)new_pa, (void*)old_pa, PGSIZE);
                    
                    // 映射到新页表
                    if (map_page(new, va, new_pa, PTE_R | PTE_W | PTE_U) != 0) {
                        printf("uvm_copy_pgtbl: failed to map mmap page at 0x%lx\n", va);
                        pmem_free(new_pa, false);  // ✅ 修复：添加 in_kernel 参数
                    }
                } else {
                    printf("uvm_copy_pgtbl: failed to allocate page for mmap\n");
                }
            }
        }
        
        region = region->next;
    }
    
    printf("uvm_copy_pgtbl: page table copy completed\n");
}

// ================================
// 内存映射函数
// ================================

void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    printf("uvm_mmap: mapping %d pages at 0x%lx with permissions 0x%x\n", 
           npages, begin, perm);
    
    proc_t* p = myproc();
    if (!p) {
        printf("uvm_mmap: no current process\n");
        return;
    }
    
    // 检查地址对齐
    if (begin % PGSIZE != 0) {
        printf("uvm_mmap: address not page-aligned\n");
        return;
    }
    
    // 转换权限标志
    uint64 pte_flags = PTE_U;  // 用户可访问
    if (perm & 0x1) pte_flags |= PTE_R;  // 可读
    if (perm & 0x2) pte_flags |= PTE_W;  // 可写
    if (perm & 0x4) pte_flags |= PTE_X;  // 可执行
    
    // 为每个页面分配物理内存并映射
    for (uint32 i = 0; i < npages; i++) {
        uint64 va = begin + i * PGSIZE;
        uint64 pa = (uint64)pmem_alloc(false);
        
        if (!pa) {
            printf("uvm_mmap: failed to allocate physical page %d\n", i);
            // 清理已分配的页面
            for (uint32 j = 0; j < i; j++) {
                uint64 cleanup_va = begin + j * PGSIZE;
                uint64 cleanup_pa = va_to_pa(p->pgtbl, cleanup_va);
                if (cleanup_pa) {
                    unmap_page(p->pgtbl, cleanup_va);
                    pmem_free(cleanup_pa, false);  // ✅ 修复：添加 in_kernel 参数
                }
            }
            return;
        }
        
        // 清零页面
        memset((void*)pa, 0, PGSIZE);
        
        // 映射页面
        if (map_page(p->pgtbl, va, pa, pte_flags) != 0) {
            printf("uvm_mmap: failed to map page at 0x%lx\n", va);
            pmem_free(pa, false);  // ✅ 修复：添加 in_kernel 参数
            // 清理已分配的页面
            for (uint32 j = 0; j < i; j++) {
                uint64 cleanup_va = begin + j * PGSIZE;
                uint64 cleanup_pa = va_to_pa(p->pgtbl, cleanup_va);
                if (cleanup_pa) {
                    unmap_page(p->pgtbl, cleanup_va);
                    pmem_free(cleanup_pa, false);  // ✅ 修复：添加 in_kernel 参数
                }
            }
            return;
        }
    }
    
    printf("uvm_mmap: successfully mapped %d pages\n", npages);
}

void uvm_munmap(uint64 begin, uint32 npages)
{
    printf("uvm_munmap: unmapping %d pages at 0x%lx\n", npages, begin);
    
    proc_t* p = myproc();
    if (!p) {
        printf("uvm_munmap: no current process\n");
        return;
    }
    
    // 检查地址对齐
    if (begin % PGSIZE != 0) {
        printf("uvm_munmap: address not page-aligned\n");
        return;
    }
    
    // 取消映射并释放物理页面
    for (uint32 i = 0; i < npages; i++) {
        uint64 va = begin + i * PGSIZE;
        uint64 pa = va_to_pa(p->pgtbl, va);
        
        if (pa != 0) {
            // 取消映射
            unmap_page(p->pgtbl, va);
            
            // 释放物理页面
            pmem_free(pa, false);  // ✅ 修复：添加 in_kernel 参数
            
            printf("uvm_munmap: unmapped page at 0x%lx (pa=0x%lx)\n", va, pa);
        } else {
            printf("uvm_munmap: page at 0x%lx not mapped\n", va);
        }
    }
    
    printf("uvm_munmap: unmapping completed\n");
}

// ================================
// 堆管理函数
// ================================

uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    printf("uvm_heap_grow: growing heap by %d bytes from 0x%lx\n", len, heap_top);
    
    if (!pgtbl) {
        printf("uvm_heap_grow: invalid page table\n");
        return heap_top;
    }
    
    uint64 new_heap_top = heap_top + len;
    uint64 old_heap_pages = (heap_top - USER_HEAP_BASE + PGSIZE - 1) / PGSIZE;
    uint64 new_heap_pages = (new_heap_top - USER_HEAP_BASE + PGSIZE - 1) / PGSIZE;
    
    printf("uvm_heap_grow: old pages=%ld, new pages=%ld\n", old_heap_pages, new_heap_pages);
    
    // 分配新的页面
    for (uint64 i = old_heap_pages; i < new_heap_pages; i++) {
        uint64 va = USER_HEAP_BASE + i * PGSIZE;
        uint64 pa = (uint64)pmem_alloc(false);
        
        if (!pa) {
            printf("uvm_heap_grow: failed to allocate page %ld\n", i);
            // 清理已分配的页面
            for (uint64 j = old_heap_pages; j < i; j++) {
                uint64 cleanup_va = USER_HEAP_BASE + j * PGSIZE;
                uint64 cleanup_pa = va_to_pa(pgtbl, cleanup_va);
                if (cleanup_pa) {
                    unmap_page(pgtbl, cleanup_va);
                    pmem_free(cleanup_pa, false);  // ✅ 修复：添加 in_kernel 参数
                }
            }
            return heap_top;  // 返回原来的堆顶
        }
        
        // 清零页面
        memset((void*)pa, 0, PGSIZE);
        
        // 映射页面
        if (map_page(pgtbl, va, pa, PTE_R | PTE_W | PTE_U) != 0) {
            printf("uvm_heap_grow: failed to map page at 0x%lx\n", va);
            pmem_free(pa, false);  // ✅ 修复：添加 in_kernel 参数
            // 清理已分配的页面
            for (uint64 j = old_heap_pages; j < i; j++) {
                uint64 cleanup_va = USER_HEAP_BASE + j * PGSIZE;
                uint64 cleanup_pa = va_to_pa(pgtbl, cleanup_va);
                if (cleanup_pa) {
                    unmap_page(pgtbl, cleanup_va);
                    pmem_free(cleanup_pa, false);  // ✅ 修复：添加 in_kernel 参数
                }
            }
            return heap_top;  // 返回原来的堆顶
        }
        
        printf("uvm_heap_grow: allocated and mapped page %ld at 0x%lx\n", i, va);
    }
    
    printf("uvm_heap_grow: heap grown to 0x%lx\n", new_heap_top);
    return new_heap_top;
}

uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    printf("uvm_heap_ungrow: shrinking heap by %d bytes from 0x%lx\n", len, heap_top);
    
    if (!pgtbl) {
        printf("uvm_heap_ungrow: invalid page table\n");
        return heap_top;
    }
    
    uint64 new_heap_top = heap_top - len;
    
    // 确保不会收缩到堆基址以下
    if (new_heap_top < USER_HEAP_BASE) {
        printf("uvm_heap_ungrow: cannot shrink below heap base\n");
        new_heap_top = USER_HEAP_BASE;
    }
    
    uint64 old_heap_pages = (heap_top - USER_HEAP_BASE + PGSIZE - 1) / PGSIZE;
    uint64 new_heap_pages = (new_heap_top - USER_HEAP_BASE + PGSIZE - 1) / PGSIZE;
    
    printf("uvm_heap_ungrow: old pages=%ld, new pages=%ld\n", old_heap_pages, new_heap_pages);
    
    // 释放不需要的页面
    for (uint64 i = new_heap_pages; i < old_heap_pages; i++) {
        uint64 va = USER_HEAP_BASE + i * PGSIZE;
        uint64 pa = va_to_pa(pgtbl, va);
        
        if (pa != 0) {
            // 取消映射
            unmap_page(pgtbl, va);
            
            // 释放物理页面
            pmem_free(pa, false);  // ✅ 修复：添加 in_kernel 参数
            
            printf("uvm_heap_ungrow: freed page %ld at 0x%lx\n", i, va);
        }
    }
    
    printf("uvm_heap_ungrow: heap shrunk to 0x%lx\n", new_heap_top);
    return new_heap_top;
}

// ================================
// 数据拷贝函数
// ================================

void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    /*printf("uvm_copyin: copying %d bytes from user 0x%lx to kernel 0x%lx\n", 
           len, src, dst);*/
    
    if (!pgtbl) {
        //printf("uvm_copyin: invalid page table\n");
        return;
    }
    
    char* dst_ptr = (char*)dst;
    uint32 copied = 0;
    
    while (copied < len) {
        // 计算当前页面的虚拟地址和偏移
        uint64 va = src + copied;
        uint64 page_va = va & ~(PGSIZE - 1);
        uint64 offset = va & (PGSIZE - 1);
        
        // 获取物理地址
        uint64 pa = va_to_pa(pgtbl, page_va);
        if (pa == 0) {
            //printf("uvm_copyin: page not mapped at 0x%lx\n", page_va);
            return;
        }
        
        // 计算本次拷贝的字节数
        uint32 copy_len = PGSIZE - offset;
        if (copy_len > len - copied) {
            copy_len = len - copied;
        }
        
        // 拷贝数据
        char* src_ptr = (char*)(pa + offset);
        memcpy(dst_ptr + copied, src_ptr, copy_len);
        
        copied += copy_len;
    }
    
    //printf("uvm_copyin: copied %d bytes successfully\n", copied);
}

void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    printf("uvm_copyout: copying %d bytes from kernel 0x%lx to user 0x%lx\n", 
           len, src, dst);
    
    if (!pgtbl) {
        printf("uvm_copyout: invalid page table\n");
        return;
    }
    
    char* src_ptr = (char*)src;
    uint32 copied = 0;
    
    while (copied < len) {
        // 计算当前页面的虚拟地址和偏移
        uint64 va = dst + copied;
        uint64 page_va = va & ~(PGSIZE - 1);
        uint64 offset = va & (PGSIZE - 1);
        
        // 获取物理地址
        uint64 pa = va_to_pa(pgtbl, page_va);
        if (pa == 0) {
            printf("uvm_copyout: page not mapped at 0x%lx\n", page_va);
            return;
        }
        
        // 计算本次拷贝的字节数
        uint32 copy_len = PGSIZE - offset;
        if (copy_len > len - copied) {
            copy_len = len - copied;
        }
        
        // 拷贝数据
        char* dst_ptr = (char*)(pa + offset);
        memcpy(dst_ptr, src_ptr + copied, copy_len);
        
        copied += copy_len;
    }
    
    printf("uvm_copyout: copied %d bytes successfully\n", copied);
}

void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    printf("uvm_copyin_str: copying string from user 0x%lx to kernel 0x%lx (max %d bytes)\n", 
           src, dst, maxlen);
    
    if (!pgtbl) {
        printf("uvm_copyin_str: invalid page table\n");
        return;
    }
    
    char* dst_ptr = (char*)dst;
    uint32 copied = 0;
    
    while (copied < maxlen - 1) {  // 保留一个字节给 null terminator
        // 计算当前页面的虚拟地址和偏移
        uint64 va = src + copied;
        uint64 page_va = va & ~(PGSIZE - 1);
        uint64 offset = va & (PGSIZE - 1);
        
        // 获取物理地址
        uint64 pa = va_to_pa(pgtbl, page_va);
        if (pa == 0) {
            printf("uvm_copyin_str: page not mapped at 0x%lx\n", page_va);
            break;
        }
        
        // 获取字符
        char c = *((char*)(pa + offset));
        dst_ptr[copied] = c;
        copied++;
        
        // 如果遇到字符串结束符，停止拷贝
        if (c == '\0') {
            break;
        }
    }
    
    // 确保字符串以 null 结尾
    if (copied == maxlen - 1) {
        dst_ptr[copied] = '\0';
        copied++;
    }
    
    printf("uvm_copyin_str: copied string \"%s\" (%d bytes)\n", dst_ptr, copied);
}
