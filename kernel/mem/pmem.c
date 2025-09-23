#include "mem/pmem.h"
#include "mem/str.h"
#include "lib/lock.h"
#include "lib/print.h"

// 页面节点结构：利用空闲页面前8字节存储链表指针
typedef struct page_node {
    struct page_node* next;
} page_node_t;

// 内存区域管理结构
typedef struct alloc_region {
    uint64 begin;               // 起始物理地址
    uint64 end;                 // 结束物理地址
    uint32 total_pages;         // 总页面数
    spinlock_t lock;            // 保护临界区的自旋锁
    uint32 free_pages;          // 当前空闲页面数
    uint32 alloc_count;         // 分配计数器
    uint32 free_count;          // 释放计数器
    page_node_t* list_head;     // 空闲链表头
    const char* name;           // 区域名称（调试用）
} alloc_region_t;

// 内核和用户物理页分开管理
static alloc_region_t kern_region, user_region;

// 私有函数声明
static void region_init(alloc_region_t* region, uint64 begin, uint64 end, const char* name);
static void region_add_pages(alloc_region_t* region);
static void* region_alloc(alloc_region_t* region);
static void region_free(alloc_region_t* region, void* page);
static void* region_alloc_pages(alloc_region_t* region, int n);
static void region_free_pages(alloc_region_t* region, void* pages, int n);

void pmem_init(void)
{
    // 计算内存布局
    uint64 alloc_begin = PGROUNDUP((uint64)ALLOC_BEGIN);
    uint64 alloc_end = PGROUNDDOWN((uint64)ALLOC_END);
    uint64 total_pages = (alloc_end - alloc_begin) / PGSIZE;
    
    printf("=== Physical Memory Manager Initialization ===\n");
    printf("Memory layout:\n");
    printf("  KERNEL_DATA:  0x%p\n", KERNEL_DATA);
    printf("  ALLOC_BEGIN:  0x%p (aligned: 0x%p)\n", ALLOC_BEGIN, (void*)alloc_begin);
    printf("  ALLOC_END:    0x%p (aligned: 0x%p)\n", ALLOC_END, (void*)alloc_end);
    printf("  Total memory: %d MB\n", (alloc_end - alloc_begin) / (1024*1024));
    printf("  Total pages:  %d\n", total_pages);
    
    // 检查内存是否足够
    if (total_pages < KERNEL_PAGES + 1) {
        panic("pmem_init: not enough memory for kernel pages");
    }
    
    // 初始化内核区域
    uint64 kern_end = alloc_begin + KERNEL_PAGES * PGSIZE;
    region_init(&kern_region, alloc_begin, kern_end, "kernel");
    
    // 初始化用户区域
    region_init(&user_region, kern_end, alloc_end, "user");
    
    // 添加所有页面到空闲链表
    region_add_pages(&kern_region);
    region_add_pages(&user_region);
    
    printf("Region configuration:\n");
    printf("  Kernel: %d pages (%d MB)\n", kern_region.total_pages, 
           kern_region.total_pages * 4 / 1024);
    printf("  User:   %d pages (%d MB)\n", user_region.total_pages,
           user_region.total_pages * 4 / 1024);
    
    printf("Physical memory manager initialized successfully!\n");
    printf("===============================================\n");
}

void* pmem_alloc(bool in_kernel)
{
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    return region_alloc(region);
}

void pmem_free(void* page, bool in_kernel)
{
    if (page == NULL) {
        return;
    }
    
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    region_free(region, page);
}

void* pmem_alloc_pages(int n, bool in_kernel)
{
    if (n <= 0) {
        return NULL;
    }
    
    if (n == 1) {
        return pmem_alloc(in_kernel);
    }
    
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    return region_alloc_pages(region, n);
}

void pmem_free_pages(void* pages, int n, bool in_kernel)
{
    if (pages == NULL || n <= 0) {
        return;
    }
    
    if (n == 1) {
        pmem_free(pages, in_kernel);
        return;
    }
    
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    region_free_pages(region, pages, n);
}

void pmem_stats(void)
{
    printf("=== Physical Memory Statistics ===\n");
    
    spinlock_acquire(&kern_region.lock);
    printf("Kernel region (%s):\n", kern_region.name);
    printf("  Total pages:     %d\n", kern_region.total_pages);
    printf("  Free pages:      %d\n", kern_region.free_pages);
    printf("  Used pages:      %d\n", kern_region.total_pages - kern_region.free_pages);
    printf("  Allocations:     %d\n", kern_region.alloc_count);
    printf("  Deallocations:   %d\n", kern_region.free_count);
    
    // 避免浮点运算，使用整数百分比
    uint32 used_pages = kern_region.total_pages - kern_region.free_pages;
    uint32 usage_percent = (used_pages * 100) / kern_region.total_pages;
    printf("  Usage: %d%%\n", usage_percent);
    spinlock_release(&kern_region.lock);

    spinlock_acquire(&user_region.lock);
    printf("User region (%s):\n", user_region.name);
    printf("  Total pages:     %d\n", user_region.total_pages);
    printf("  Free pages:      %d\n", user_region.free_pages);
    printf("  Used pages:      %d\n", user_region.total_pages - user_region.free_pages);
    printf("  Allocations:     %d\n", user_region.alloc_count);
    printf("  Deallocations:   %d\n", user_region.free_count);
    
    used_pages = user_region.total_pages - user_region.free_pages;
    usage_percent = (used_pages * 100) / user_region.total_pages;
    printf("  Usage: %d%%\n", usage_percent);
    spinlock_release(&user_region.lock);
    
    printf("================================\n");
}

int pmem_available(bool in_kernel)
{
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    
    spinlock_acquire(&region->lock);
    int available = region->free_pages;
    spinlock_release(&region->lock);
    
    return available;
}

bool pmem_is_valid_page(void* page)
{
    uint64 pa = (uint64)page;
    
    // 检查页面对齐
    if (pa % PGSIZE != 0) {
        return false;
    }
    
    // 检查是否在内核区域
    if (pa >= kern_region.begin && pa < kern_region.end) {
        return true;
    }
    
    // 检查是否在用户区域
    if (pa >= user_region.begin && pa < user_region.end) {
        return true;
    }
    
    return false;
}

// 私有函数实现

static void region_init(alloc_region_t* region, uint64 begin, uint64 end, const char* name)
{
    region->begin = begin;
    region->end = end;
    region->total_pages = (end - begin) / PGSIZE;
    region->free_pages = region->total_pages;
    region->alloc_count = 0;
    region->free_count = 0;
    region->list_head = NULL;
    region->name = name;
    
    spinlock_init(&region->lock, (char*)name);
}

static void region_add_pages(alloc_region_t* region)
{
    // 将所有页面加入空闲链表（LIFO顺序）
    for (uint64 pa = region->begin; pa < region->end; pa += PGSIZE) {
        // 清零页面内容
        memset((void*)pa, 0, PGSIZE);
        
        // 将页面加入链表头部
        page_node_t* node = (page_node_t*)pa;
        node->next = region->list_head;
        region->list_head = node;
    }
}

static void* region_alloc(alloc_region_t* region)
{
    page_node_t* page = NULL;
    
    spinlock_acquire(&region->lock);
    
    if (region->list_head != NULL) {
        // 从链表头取出一个页面
        page = region->list_head;
        region->list_head = page->next;
        region->free_pages--;
        region->alloc_count++;
    }
    
    spinlock_release(&region->lock);
    
    if (page) {
        // 清零页面（安全起见）
        memset((void*)page, 0, PGSIZE);
    }
    
    return (void*)page;
}

static void region_free(alloc_region_t* region, void* page)
{
    uint64 pa = (uint64)page;
    
    // 参数检查
    if (pa % PGSIZE != 0) {
        printf("ERROR: page 0x%lx not aligned to page boundary\n", pa);
        panic("region_free: page not aligned");
    }
    
    if (pa < region->begin || pa >= region->end) {
        printf("ERROR: Attempting to free page 0x%lx to %s region\n", pa, region->name);
        printf("  Region range: 0x%lx - 0x%lx\n", region->begin, region->end);
        printf("  Kernel range: 0x%lx - 0x%lx\n", kern_region.begin, kern_region.end);
        printf("  User range:   0x%lx - 0x%lx\n", user_region.begin, user_region.end);
        panic("region_free: page out of region");
    }
    
    // 填充调试模式（有助于发现use-after-free错误）
    memset(page, 0xDD, PGSIZE);
    
    // 将页面加入链表头部
    page_node_t* node = (page_node_t*)page;
    
    spinlock_acquire(&region->lock);
    node->next = region->list_head;
    region->list_head = node;
    region->free_pages++;
    region->free_count++;
    spinlock_release(&region->lock);
}

static void* region_alloc_pages(alloc_region_t* region, int n)
{
    // 简单实现：逐个分配页面，检查是否连续
    // 注意：这不是最优算法，但能满足基本需求
    
    void** pages = pmem_alloc(true);  // 临时存储页面指针
    if (!pages) {
        return NULL;
    }
    
    uint64 first_pa = 0;
    bool found_continuous = false;
    
    // 尝试找到连续的n个页面
    for (int attempt = 0; attempt < 10 && !found_continuous; attempt++) {
        // 分配n个页面
        for (int i = 0; i < n; i++) {
            pages[i] = region_alloc(region);
            if (!pages[i]) {
                // 分配失败，释放已分配的页面
                for (int j = 0; j < i; j++) {
                    region_free(region, pages[j]);
                }
                pmem_free(pages, true);
                return NULL;
            }
        }
        
        // 检查是否连续
        first_pa = (uint64)pages[0];
        found_continuous = true;
        for (int i = 1; i < n; i++) {
            if ((uint64)pages[i] != first_pa + i * PGSIZE) {
                found_continuous = false;
                break;
            }
        }
        
        if (!found_continuous) {
            // 不连续，释放所有页面，重新尝试
            for (int i = 0; i < n; i++) {
                region_free(region, pages[i]);
            }
        }
    }
    
    pmem_free(pages, true);
    
    if (found_continuous) {
        return (void*)first_pa;
    } else {
        printf("Warning: failed to allocate %d continuous pages\n", n);
        return NULL;
    }
}

static void region_free_pages(alloc_region_t* region, void* pages, int n)
{
    uint64 pa = (uint64)pages;
    
    // 释放连续的n个页面
    for (int i = 0; i < n; i++) {
        region_free(region, (void*)(pa + i * PGSIZE));
    }
}

/**
 * 检查页面属于哪个区域
 * @param page 页面地址
 * @return true表示内核区域，false表示用户区域，如果无效返回false
 */
bool pmem_is_kernel_page(void* page)
{
    uint64 pa = (uint64)page;
    
    if (pa >= kern_region.begin && pa < kern_region.end) {
        return true;   // 内核页面
    } else if (pa >= user_region.begin && pa < user_region.end) {
        return false;  // 用户页面
    } else {
        printf("ERROR: Page 0x%lx is not in any valid region\n", pa);
        printf("  Kernel range: 0x%lx - 0x%lx\n", kern_region.begin, kern_region.end);
        printf("  User range:   0x%lx - 0x%lx\n", user_region.begin, user_region.end);
        panic("pmem_is_kernel_page: invalid page address");
        return false;  // 这行不会执行，但能让编译器满意
    }
}

void pmem_free_safe(void* page)
{
    if (!page) return;
    
    // 自动检测页面归属
    bool is_kernel = pmem_is_kernel_page(page);
    pmem_free(page, is_kernel);
}