#ifndef __PMEM_H__
#define __PMEM_H__

#include "common.h"

// 来自kernel.ld的内存布局标记
extern char KERNEL_DATA[];
extern char ALLOC_BEGIN[];
extern char ALLOC_END[];

// 物理内存管理接口
void  pmem_init(void);
void* pmem_alloc(bool in_kernel);
void  pmem_free(void* page, bool in_kernel);

// 扩展接口（连续页面分配）
void* pmem_alloc_pages(int n, bool in_kernel); // 分配连续的n个页面
void  pmem_free_pages(void* pages, int n, bool in_kernel); // 释放连续的n个页面

// 查询和调试接口
void  pmem_stats(void);                   // 打印内存统计信息
int   pmem_available(bool in_kernel);     // 获取可用页面数
bool  pmem_is_valid_page(void* page);     // 检查页面地址是否有效

// 任务3要求的标准接口（兼容性封装）
static inline void  pmm_init(void) { pmem_init(); }
static inline void* alloc_page(void) { return pmem_alloc(true); }
static inline void  free_page(void* page) { pmem_free(page, true); }

#endif
