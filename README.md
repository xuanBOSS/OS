# LAB-2：页表与内存管理 

**本阶段目标：**

在机器启动的基础上，实现对物理内存的管理，对内核构建页表，并让所有CPU启用页表。本阶段不产生额外的输出，但启用页表后，只要程序执行不错乱，仍可把提示语输出到屏幕上，就基本说明本阶段任务正确完成。

## 1、物理内存管理
### 1. 设计要求

#### a、内存布局设计

基于安全隔离的原则，物理内存被分为三个区域：

```
物理内存布局：
0x80000000 ┌─────────────────┐ KERNEL_BASE
           │   内核代码       │ (.text)
KERNEL_DATA├─────────────────┤ 
           │   内核数据       │ (.data, .bss)
ALLOC_BEGIN├─────────────────┤ ← 可分配内存开始
           │                 │
           │   内核页面池      │ (KERNEL_PAGES个页面)
           │                 │
           ├─────────────────┤ 
           │                 │
           │   用户页面池      │ (剩余页面)
           │                 │
ALLOC_END  └─────────────────┘ ← 物理内存结束
```
**实现细节：**

```
// kernel.ld - 链接脚本提供内存布局标记
PROVIDE(KERNEL_DATA = .);     // 内核数据开始位置
PROVIDE(ALLOC_BEGIN = .);     // 可分配内存开始
PROVIDE(ALLOC_END = 0x88000000); // 物理内存结束（128MB）
```

**运行结果验证：**

```
Total memory: 127 MB
Total pages:  32760
Kernel: 1024 pages (4 MB)    # 内核专用
User:   31736 pages (123 MB) # 用户专用
```

#### b、合适的数据结构

**分离管理策略**

为了防止用户程序耗尽内核内存，采用分离管理：

- **kern_region**: 管理内核专用页面
- **user_region**: 管理用户专用页面
- **in_kernel参数**: 决定从哪个池分配/释放

```
// 利用空闲页面本身存储链表节点
typedef struct page_node {
    struct page_node* next;  // 仅8字节，存储在页面开头
} page_node_t;

// 内存区域管理结构
typedef struct alloc_region {
    uint64 begin, end;           // 内存范围
    uint32 total_pages;          // 总页面数
    spinlock_t lock;             // 并发保护
    uint32 free_pages;           // 空闲页面计数
    uint32 alloc_count, free_count; // 统计信息
    page_node_t* list_head;      // 空闲链表头
    const char* name;            // 调试标识
} alloc_region_t;
```

#### c、分配和释放接口

**标准接口实现：**

```
// 任务要求的标准接口
void  pmm_init(void);           // 初始化内存管理器
void* alloc_page(void);         // 分配一个物理页
void  free_page(void* page);    // 释放一个物理页
void* alloc_pages(int n);       // 分配连续的n个页面

// 扩展接口
void* pmem_alloc(bool in_kernel);       // 指定从哪个池分配
void  pmem_free(void* page, bool in_kernel); // 指定释放到哪个池
void  pmem_stats(void);                 // 内存使用统计
```

### 2、核心问题

#### a、可用内存范围

**链接脚本 + 运行时计算**

```
void pmem_init(void) {
    // 从链接脚本获取内存边界
    uint64 alloc_begin = PGROUNDUP((uint64)ALLOC_BEGIN);
    uint64 alloc_end = PGROUNDDOWN((uint64)ALLOC_END);
    uint64 total_pages = (alloc_end - alloc_begin) / PGSIZE;
    
    // 页面对齐确保内存管理的正确性
    // PGROUNDUP: 向上对齐到页边界
    // PGROUNDDOWN: 向下对齐到页边界
}
```

**实现逻辑：**

1. 链接脚本在编译时确定内存布局
2. 运行时读取这些符号获得精确的内存范围
3. 自动计算总可用页面数和分区大小

#### b、处理内存碎片

**外部碎片解决：**

- 固定4KB页面分配，从根源避免外部碎片
- 所有分配都是页面对齐的，不会产生小碎片

**内部碎片处理：**

- 当前阶段：接受内部碎片，简化实现
- 未来扩展：可以添加slab分配器处理小对象

**连续页面分配：**

```
static void* region_alloc_pages(alloc_region_t* region, int n) {
    // 尝试分配算法：
    // 1. 分配n个单页
    // 2. 检查物理地址是否连续
    // 3. 如果不连续，释放重试（最多10次）
    // 4. 返回连续块的起始地址
}
```

#### c、支持不同大小的分配

**多层次支持**

```
// 1. 单页分配（主要接口）
void* pmem_alloc(bool in_kernel);

// 2. 连续多页分配
void* pmem_alloc_pages(int n, bool in_kernel);

// 3. 标准接口兼容
static inline void* alloc_page(void) { 
    return pmem_alloc(true); 
}
```

- **当前阶段**：专注页面级分配，满足内核基本需求
- **未来扩展**：为上层应用预留接口扩展空间

### 3、实现策略

#### a、链表方案

**侵入式链表设计：**

```
// 空闲页面自身存储链表节点
static void region_add_pages(alloc_region_t* region) {
    for (uint64 pa = region->begin; pa < region->end; pa += PGSIZE) {
        memset((void*)pa, 0, PGSIZE);        // 清零页面
        page_node_t* node = (page_node_t*)pa; // 转换为链表节点
        node->next = region->list_head;       // 头插法
        region->list_head = node;
    }
}
```

**分配逻辑：**

```
static void* region_alloc(alloc_region_t* region) {
    spinlock_acquire(&region->lock);
    if (region->list_head != NULL) {
        page_node_t* page = region->list_head;  // 取链表头
        region->list_head = page->next;         // 更新链表头
        region->free_pages--;                   // 更新计数
    }
    spinlock_release(&region->lock);
    return (void*)page;
}
```

#### b、错误检查

**多层次错误检查：**

```
// 1. 参数有效性检查
void pmem_free(void* page, bool in_kernel) {
    if (page == NULL) return;  // NULL指针安全处理
    
    uint64 pa = (uint64)page;
    if (pa % PGSIZE != 0) {    // 页面对齐检查
        panic("region_free: page not aligned");
    }
    
    if (pa < region->begin || pa >= region->end) { // 范围检查
        panic("region_free: page out of region");
    }
}

// 2. 调试辅助
memset(page, 0xDD, PGSIZE);  // 填充特殊值，便于调试
```

#### c、性能优化

- **O(1)时间复杂度**：所有操作都是常数时间
- **减少内存访问**：利用LIFO策略提高缓存局部性
- **最小化锁竞争**：分离的锁减少多核竞争
- **零拷贝**：直接操作物理地址，无额外复制

### 4、代码逻辑详解

#### （1）mem/str.h 和 str.c - 基础内存操作

**设计目的：** 提供独立的内存操作函数，避免依赖标准库

**核心函数：**

```
void* memset(void* dst, int c, uint64 n);  // 内存填充
void* memcpy(void* dst, const void* src, uint64 n); // 内存复制
int memcmp(const void* s1, const void* s2, uint64 n); // 内存比较
```

**实现逻辑：** 简单的字节操作循环，确保在裸机环境下可靠工作

#### （2）mem/pmem.h - 接口设计

**分层设计：**

```
// 底层实现接口
void* pmem_alloc(bool in_kernel);     // 支持内核/用户分离
void pmem_free(void* page, bool in_kernel);

// 标准兼容接口
static inline void* alloc_page(void) { return pmem_alloc(true); }

// 调试和统计接口
void pmem_stats(void);               // 运行时统计
bool pmem_is_valid_page(void* page); // 页面有效性检查
```

#### （3）common.h - 系统常量定义

**新增定义：**

```
#define PGSIZE 4096              // 页面大小：4KB
#define PGSHIFT 12               // 页面位移：log2(4096)
#define PGROUNDUP(sz)   (((sz)+PGSIZE-1) & ~(PGSIZE-1))   // 向上对齐
#define PGROUNDDOWN(a)  (((a)) & ~(PGSIZE-1))             // 向下对齐
#define KERNEL_PAGES 1024        // 内核保留页面数
```

**设计考虑：** 这些常量是整个内存管理系统的基础，需要在全局可见

#### （4）kernel.ld - 内存布局脚本

**关键标记：**

```
PROVIDE(KERNEL_DATA = .);        // 内核数据段开始
PROVIDE(ALLOC_BEGIN = .);        // 可分配内存开始
PROVIDE(ALLOC_END = 0x88000000); // 物理内存结束
```

**设计逻辑：**

- 编译时确定内存布局
- 运行时通过extern变量访问这些地址
- 自动计算各区域大小

#### （5）pmem.c - 核心实现逻辑

**初始化流程：**

```
void pmem_init(void) {
    // 1. 计算内存布局
    uint64 alloc_begin = PGROUNDUP((uint64)ALLOC_BEGIN);
    uint64 alloc_end = PGROUNDDOWN((uint64)ALLOC_END);
    
    // 2. 分割内存区域
    uint64 kern_end = alloc_begin + KERNEL_PAGES * PGSIZE;
    region_init(&kern_region, alloc_begin, kern_end, "kernel");
    region_init(&user_region, kern_end, alloc_end, "user");
    
    // 3. 构建空闲链表
    region_add_pages(&kern_region);
    region_add_pages(&user_region);
}
```

**分配算法：**

1. **单页分配**：从链表头取出，O(1)时间
2. **连续分配**：多次尝试+连续性检查
3. **错误处理**：完整的参数验证和异常处理


## 2、构建内核页表
### 1、架构设计

#### **整体架构**

```
┌─────────────────────────────────────┐
│        内核虚拟内存层                 │
│     kvm_init, kvm_inithart          │
├─────────────────────────────────────┤
│        页表管理层                     │
│  create_pagetable, map_page         │
├─────────────────────────────────────┤
│        页表遍历层                     │
│   walk_create, walk_lookup          │
├─────────────────────────────────────┤
│        地址转换层                     │
│    VPN提取, PA⇔PTE转换               │
├─────────────────────────────────────┤
│        物理内存管理层                 │
│      pmem_alloc/free                │
└─────────────────────────────────────┘
```

### 2、地址解析实现

#### 虚拟地址

RISC-V Sv39使用39位虚拟地址，采用三级页表结构：

```
VA[38:30] → VPN[2] (Level-2) → 根页表索引
VA[29:21] → VPN[1] (Level-1) → 中间页表索引  
VA[20:12] → VPN[0] (Level-0) → 叶子页表索引
VA[11:0]  → Offset → 页内偏移
```

#### 关键宏定义实现

```
// 计算各级页表索引提取位移量
#define VPN_SHIFT(level) (12 + 9 * (level))

// 从虚拟地址提取指定级别的9位页表索引
#define VPN_MASK(va, level) (((va) >> VPN_SHIFT(level)) & 0x1FF)

// 实际计算过程：
// Level-2: (va >> 30) & 0x1FF  → 根页表索引
// Level-1: (va >> 21) & 0x1FF  → 中间页表索引
// Level-0: (va >> 12) & 0x1FF  → 叶子页表索引
```

#### 物理地址与PTE转换

```
// PA→PTE: 将物理地址转换为页表项格式
#define PA_TO_PTE(pa) ((((uint64)(pa)) >> 12) << 10)

// PTE→PA: 从页表项提取物理地址  
#define PTE_TO_PA(pte) (((pte) >> 10) << 12)

// 转换逻辑：
// 1. 物理地址右移12位得到PPN (Physical Page Number)
// 2. PPN左移10位放入PTE的[53:10]位置
```

### 3、页表遍历算法实现

#### 创建模式遍历 (walk_create)

这是页表系统的核心算法，支持按需创建中间页表：

```
pte_t* walk_create(pagetable_t pt, uint64 va)
{
    if (va >= VA_MAX) return NULL;
    
    // 三级页表遍历：Level-2 → Level-1 → Level-0
    for (int level = 2; level > 0; level--) {
        uint64 index = VPN_MASK(va, level);    // 提取当前级索引
        pte_t* pte = &pt[index];               // 定位PTE
        
        if (*pte & PTE_V) {
            // 页表项有效，继续向下遍历
            pt = (pagetable_t)PTE_TO_PA(*pte);
        } else {
            // 页表项无效，创建新页表
            pagetable_t new_pt = create_pagetable();
            if (!new_pt) return NULL;
            
            // 设置PTE指向新页表，只设置V位
            *pte = PA_TO_PTE((uint64)new_pt) | PTE_V;
            pt = new_pt;
        }
    }
    
    // 返回Level-0页表中的PTE地址
    return &pt[VPN_MASK(va, 0)];
}
```

**关键实现要点：**

1. **逐级下降**：从Level-2开始，逐级向Level-0遍历
2. **按需创建**：遇到无效PTE时，分配新页表并建立连接
3. **中间节点标记**：中间级PTE只设置V位，不设置R/W/X
4. **错误处理**：内存分配失败时立即返回

#### 查询模式遍历 (walk_lookup)

只读遍历，不创建新页表：

```
pte_t* walk_lookup(pagetable_t pt, uint64 va)
{
    if (va >= VA_MAX) return NULL;
    
    // 逐级查找，不创建新页表
    for (int level = 2; level > 0; level--) {
        uint64 index = VPN_MASK(va, level);
        pte_t* pte = &pt[index];
        
        if (*pte & PTE_V) {
            pt = (pagetable_t)PTE_TO_PA(*pte);
        } else {
            return NULL;  // 路径不存在
        }
    }
    
    // 检查叶子节点有效性
    pte_t* pte = &pt[VPN_MASK(va, 0)];
    return (*pte & PTE_V) ? pte : NULL;
}
```

### 4、映射建立实现

#### 核心映射函数

```
int map_page(pagetable_t pt, uint64 va, uint64 pa, int perm)
{
    // 1. 参数验证
    if (va % PGSIZE != 0 || pa % PGSIZE != 0) {
        return -1;  // 地址必须页对齐
    }
    
    // 2. 获取目标PTE地址
    pte_t* pte = walk_create(pt, va);
    if (!pte) {
        return -1;  // 页表创建失败
    }
    
    // 3. 检查重复映射
    if (*pte & PTE_V) {
        return -1;  // 已存在映射，拒绝重复映射
    }
    
    // 4. 建立映射：PA + 权限 + 有效位
    *pte = PA_TO_PTE(pa) | perm | PTE_V;
    return 0;
}
```

#### 映射冲突处理策略

采用**严格的冲突检测**：

- 映射前检查PTE的V位
- 如果已有有效映射，直接拒绝
- 确保映射的原子性和一致性

### 5、页表生命周期管理

#### 页表创建

```
pagetable_t create_pagetable(void)
{
    // 从内核内存池分配4KB页面
    pagetable_t pt = (pagetable_t)pmem_alloc(true);
    if (pt) {
        memset(pt, 0, PGSIZE);  // 清零所有PTE
    }
    return pt;
}
```

#### 页表销毁

实现递归销毁算法，正确处理三级页表结构：

```
void destroy_pagetable(pagetable_t pt)
{
    if (!pt) return;
    
    // 遍历当前页表的所有PTE
    for (int i = 0; i < 512; i++) {
        pte_t pte = pt[i];
        
        // 检查是否为指向下级页表的PTE
        if ((pte & PTE_V) && PTE_IS_TABLE(pte)) {
            pagetable_t child_pt = (pagetable_t)PTE_TO_PA(pte);
            destroy_pagetable(child_pt);  // 递归销毁
        }
    }
    
    pmem_free(pt, true);  // 释放当前页表
}
```

**关键判断条件：**

```
// 判断PTE是否指向中间页表
#define PTE_IS_TABLE(pte) \
    (((pte) & PTE_V) && (((pte) & (PTE_R | PTE_W | PTE_X)) == 0))
```

### 6、内核页表创建 (kvminit分析)

#### 需要映射的内存区域

基于xv6，内核页表需要映射以下关键区域：

```
void kvm_init(void)
{
    kernel_pagetable = create_pagetable();
    
    // 1. 设备寄存器映射
    kvm_map(UART0, UART0, PGSIZE, PTE_R | PTE_W);
    kvm_map(PLIC, PLIC, 0x4000000, PTE_R | PTE_W);
    
    // 2. 内核代码段映射
    extern char etext[];
    uint64 kernel_text_size = PGROUNDUP((uint64)etext - KERNBASE);
    kvm_map(KERNBASE, KERNBASE, kernel_text_size, PTE_R | PTE_X);
    
    // 3. 内核数据段和物理内存映射
    uint64 data_start = (uint64)etext;
    uint64 data_size = PHYSTOP - data_start;
    kvm_map(data_start, data_start, data_size, PTE_R | PTE_W);
}
```

#### 恒等映射的设计原因

**为什么采用恒等映射？**

1. **简化内核开发**：虚拟地址 = 物理地址，无需地址转换
2. **启动过程连续性**：启用MMU前后，代码和数据地址保持不变
3. **设备访问一致性**：硬件设备的寄存器地址保持固定
4. **内核态特权**：内核需要访问整个物理地址空间

```
物理地址布局          内核虚拟地址布局
0x10000000 UART   →   0x10000000 UART   
0x80000000 RAM    →   0x80000000 RAM    
0x88000000 End    →   0x88000000 End    
```

#### 设备内存权限设置策略

```
// UART：读写权限，用于字符输入输出
kvm_map(UART0, UART0, PGSIZE, PTE_R | PTE_W);

// PLIC：读写权限，用于中断管理
kvm_map(PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

// 内核代码：只读+可执行，防止代码被意外修改
kvm_map(KERNBASE, KERNBASE, text_size, PTE_R | PTE_X);

// 内核数据：读写权限，用于变量和堆栈
kvm_map(data_start, data_start, data_size, PTE_R | PTE_W);
```

### 7、页表激活机制(kvminithart分析)

#### SATP寄存器格式和设置

RISC-V SATP (Supervisor Address Translation and Protection) 寄存器格式：

```
[63:60] MODE  - 分页模式 (8=Sv39, 9=Sv48, 0=Bare)
[59:44] ASID  - 地址空间标识符 (进程隔离)
[43:0]  PPN   - 根页表物理页号
```

实现代码：

```
void kvm_inithart(void)
{
    int cpuid = mycpuid();
    printf("CPU%d: Enabling kernel page table...\n", cpuid);
    
    // 1. 内存屏障：确保之前的页表写入完成
    sfence_vma();
    
    // 2. 设置SATP：启用Sv39分页模式
    w_satp(MAKE_SATP(kernel_pagetable));
    
    // 3. 刷新TLB：清除旧的地址转换缓存
    sfence_vma();
    
    printf("CPU%d: Virtual memory enabled!\n", cpuid);
}

// SATP值构造宏
#define MAKE_SATP(pagetable) \
    (8L << 60 | (((uint64)(pagetable)) >> 12))
```

#### sfence.vma指令的作用

`sfence.vma` (Supervisor Fence Virtual Memory Address) 指令功能：

1. **内存屏障**：确保所有页表修改操作完成
2. **TLB刷新**：清除Translation Lookaside Buffer中的缓存条目
3. **地址转换一致性**：保证新页表立即生效

**使用时机：**

- 页表修改之前：确保之前的内存写入完成
- 页表激活之后：确保新的地址转换立即生效

#### 激活前后的注意事项

**激活前的准备：**

1. 确保所有必要的内存区域已映射
2. 验证页表结构的正确性
3. 保存当前执行上下文

**激活后的验证：**

1. 测试代码段仍可执行
2. 验证数据段仍可访问
3. 检查设备寄存器仍可操作

```
void test_virtual_memory_transition(void)
{
    // 虚拟内存启用前的分配测试
    void* pre_vm_page = pmem_alloc(true);
    
    // 启用虚拟内存
    kvm_inithart();
    
    // 虚拟内存启用后的分配测试
    void* post_vm_page = pmem_alloc(true);
    
    // 验证启用前分配的内存仍可访问
    *(char*)pre_vm_page = 0xAA;
    assert(*(char*)pre_vm_page == 0xAA);
    
    // 验证启用后分配的内存正常工作
    *(char*)post_vm_page = 0xBB;
    assert(*(char*)post_vm_page == 0xBB);
}
```

### 8、实现验证与测试

#### 测试覆盖范围

```
void test_pagetable_system(void)
{
    // 1. 页表创建/销毁测试
    pagetable_t test_pt = create_pagetable();
    
    // 2. 单页映射测试
    map_page(test_pt, 0x10000000, 0x80400000, PTE_R | PTE_W);
    
    // 3. 映射查询测试  
    pte_t* pte = walk_lookup(test_pt, 0x10000000);
    
    // 4. 权限位验证
    if (pte && (*pte & PTE_R) && (*pte & PTE_W)) { /* 验证成功 */ }
    
    // 5. 重复映射检测
    if (map_page(test_pt, 0x10000000, 0x80400000, PTE_R) != 0) {
        // 正确拒绝重复映射
    }
    
    destroy_pagetable(test_pt);
}
```
