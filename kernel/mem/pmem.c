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

void pmem_init(void)
{
    // 计算内存布局
    uint64 alloc_begin = PGROUNDUP((uint64)ALLOC_BEGIN);
    uint64 alloc_end = PGROUNDDOWN((uint64)ALLOC_END);
    uint64 total_pages = (alloc_end - alloc_begin) / PGSIZE;
    
    printf("=== Physical Memory Manager Initialization ===\n");
    printf("Memory layout:\n");
    printf("  KERNEL_DATA:  0x%lx\n", (uint64)KERNEL_DATA);          // ✅ 修复
    printf("  ALLOC_BEGIN:  0x%lx (aligned: 0x%lx)\n", (uint64)ALLOC_BEGIN, alloc_begin);  // ✅ 修复
    printf("  ALLOC_END:    0x%lx (aligned: 0x%lx)\n", (uint64)ALLOC_END, alloc_end);     // ✅ 修复
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
    printf("Adding pages to kernel region...\n");
    region_add_pages(&kern_region);
    printf("Adding pages to user region...\n");
    region_add_pages(&user_region);
    
    printf("Region configuration:\n");
    printf("  Kernel: %d pages (%d MB)\n", kern_region.total_pages, 
           kern_region.total_pages * 4 / 1024);
    printf("  User:   %d pages (%d MB)\n", user_region.total_pages,
           user_region.total_pages * 4 / 1024);
    
    // ✅ 添加：验证链表完整性
    printf("\n=== Verifying Memory Regions ===\n");
    printf("Kernel region:\n");
    printf("  begin: 0x%lx, end: 0x%lx\n", kern_region.begin, kern_region.end);
    printf("  total_pages: %d, free_pages: %d\n", kern_region.total_pages, kern_region.free_pages);
    printf("  list_head: 0x%lx\n", (uint64)kern_region.list_head);
    
    printf("User region:\n");
    printf("  begin: 0x%lx, end: 0x%lx\n", user_region.begin, user_region.end);
    printf("  total_pages: %d, free_pages: %d\n", user_region.total_pages, user_region.free_pages);
    printf("  list_head: 0x%lx\n", (uint64)user_region.list_head);
    
    // 验证前几个节点
    if (user_region.list_head) {
        printf("First 3 user pages:\n");
        page_node_t* p = user_region.list_head;
        for (int i = 0; i < 3 && p; i++) {
            printf("  [%d] page: 0x%lx, next: 0x%lx\n", 
                   i, (uint64)p, (uint64)p->next);
            p = p->next;
        }
    }
    
    printf("Physical memory manager initialized successfully!\n");
    printf("===============================================\n");
}

void* pmem_alloc(bool in_kernel)
{
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    return region_alloc(region);
}

void pmem_free(uint64 page, bool in_kernel)
{
    if (page == 0) {
        return;
    }
    
    void* page_ptr = (void*)page;
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    region_free(region, page_ptr);
}

void* pmem_alloc_pages(int n, bool in_kernel)
{
    if (n <= 0) {
        return NULL;
    }
    
    if (n == 1) {
        return pmem_alloc(in_kernel);
    }
    
    // ✅ 简化实现：逐个分配页面
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    
    // 存储分配的页面地址（使用栈数组，限制最大页面数）
    #define MAX_ALLOC_PAGES 64
    void* allocated_pages[MAX_ALLOC_PAGES];
    
    if (n > MAX_ALLOC_PAGES) {
        printf("pmem_alloc_pages: too many pages requested (%d > %d)\n", n, MAX_ALLOC_PAGES);
        return NULL;
    }
    
    // 分配n个页面
    for (int i = 0; i < n; i++) {
        allocated_pages[i] = region_alloc(region);
        if (!allocated_pages[i]) {
            // 分配失败，释放已分配的页面
            for (int j = 0; j < i; j++) {
                region_free(region, allocated_pages[j]);
            }
            return NULL;
        }
    }
    
    // 检查连续性
    uint64 first_pa = (uint64)allocated_pages[0];
    bool continuous = true;
    for (int i = 1; i < n; i++) {
        if ((uint64)allocated_pages[i] != first_pa + i * PGSIZE) {
            continuous = false;
            break;
        }
    }
    
    if (!continuous) {
        // 不连续，释放所有页面
        for (int i = 0; i < n; i++) {
            region_free(region, allocated_pages[i]);
        }
        return NULL;
    }
    
    return (void*)first_pa;
}

void pmem_free_pages(void* pages, int n, bool in_kernel)
{
    if (pages == NULL || n <= 0) {
        return;
    }
    
    uint64 pa = (uint64)pages;
    alloc_region_t* region = in_kernel ? &kern_region : &user_region;
    
    // 释放连续的n个页面
    for (int i = 0; i < n; i++) {
        region_free(region, (void*)(pa + i * PGSIZE));
    }
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
    
    // 检查是否在有效范围内
    if ((pa >= kern_region.begin && pa < kern_region.end) ||
        (pa >= user_region.begin && pa < user_region.end)) {
        return true;
    }
    
    return false;
}

bool pmem_is_kernel_page(void* page)
{
    uint64 pa = (uint64)page;
    
    if (pa >= kern_region.begin && pa < kern_region.end) {
        return true;
    } else if (pa >= user_region.begin && pa < user_region.end) {
        return false;
    } else {
        panic("pmem_is_kernel_page: invalid page address");
        return false;
    }
}

// === 私有函数实现 ===

static void region_init(alloc_region_t* region, uint64 begin, uint64 end, const char* name)
{
    region->begin = begin;
    region->end = end;
    region->total_pages = (end - begin) / PGSIZE;
    region->free_pages = region->total_pages;
    region->list_head = NULL;
    region->name = name;
    
    spinlock_init(&region->lock, (char*)name);
}

static void region_add_pages(alloc_region_t* region)
{
    // 将所有页面加入空闲链表
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
        page = region->list_head;
        region->list_head = page->next;
        region->free_pages--;
    }
    
    spinlock_release(&region->lock);
    
    if (page) {
        memset((void*)page, 0, PGSIZE);
    }
    
    return (void*)page;
}

static void region_free(alloc_region_t* region, void* page)
{
    uint64 pa = (uint64)page;
    
    // 参数检查
    if (pa % PGSIZE != 0) {
        panic("region_free: page not aligned");
    }
    
    if (pa < region->begin || pa >= region->end) {
        panic("region_free: page out of region");
    }
    
    // 填充调试模式（检测use-after-free）
    memset(page, 0xDD, PGSIZE);
    
    // 将页面加入链表头部
    page_node_t* node = (page_node_t*)page;
    
    spinlock_acquire(&region->lock);
    node->next = region->list_head;
    region->list_head = node;
    region->free_pages++;
    spinlock_release(&region->lock);
}
