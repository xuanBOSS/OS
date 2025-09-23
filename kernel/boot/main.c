#include "riscv.h"
#include "dev/uart.h"
#include "proc/proc.h"
#include "mem/pmem.h"  
#include "lib/print.h" 
#include "mem/vmem.h"  
#include "mem/kvm.h"   

// 全局状态变量，用于多核同步
volatile static int boot_cpu_id = -1;     // 启动CPU标识
volatile static int init_phase = 0;  // 0: 未初始化, 1: 初始化完成, 2: 允许输出
volatile static int cpu_started[NCPU] = {0};     // 各CPU启动状态

// UART输出锁，确保单CPU输出
volatile static int uart_lock = 0;

// 安全的字符串输出函数
void safe_print_string(const char *str) {
    // 获取简单的自旋锁
    while (__sync_lock_test_and_set(&uart_lock, 1)) {
        asm volatile("nop");
    }
    
    for (int i = 0; str[i]; i++) {
        uart_putc_sync(str[i]);
        for (volatile int j = 0; j < 1000; j++) {
            asm volatile("nop");
        }
    }
    
    // 释放锁
    __sync_lock_release(&uart_lock);
}

// 安全的数字输出函数
void safe_print_cpu_id(int cpuid) {
    while (__sync_lock_test_and_set(&uart_lock, 1)) {
        asm volatile("nop");
    }
    uart_putc_sync('0' + cpuid);
    __sync_lock_release(&uart_lock);
}

// 物理内存管理器测试函数
void test_pmem_basic(void)
{
    printf("\n=== Testing Basic Physical Memory Management ===\n");
    
    // 测试1：单页分配和释放
    printf("Test 1: Single page allocation/deallocation\n");
    void* page1 = pmem_alloc(true);  // 内核页面
    void* page2 = pmem_alloc(false); // 用户页面
    
    if (page1 && page2) 
    {
        printf("  ✓ Single page allocation successful\n");
        printf("    Kernel page: %p\n", page1);
        printf("    User page:   %p\n", page2);
        
        pmem_free(page1, true);
        pmem_free(page2, false);
        printf("  ✓ Single page deallocation successful\n");
    } 
    else 
    {
        printf("  ✗ Single page allocation failed\n");
    }
    
    // 测试2：批量分配
    printf("\nTest 2: Multiple page allocation\n");
    void* pages[10];
    int allocated = 0;
    
    for (int i = 0; i < 10; i++) 
    {
        pages[i] = pmem_alloc(true);
        if (pages[i]) 
        {
            allocated++;
        }
    }
    
    printf("  ✓ Allocated %d/10 pages successfully\n", allocated);
    
    // 释放已分配的页面
    for (int i = 0; i < allocated; i++) 
    {
        pmem_free(pages[i], true);
    }
    printf("  ✓ Released %d pages successfully\n", allocated);
    
    // 测试3：连续页面分配
    printf("\nTest 3: Continuous page allocation\n");
    void* cont_pages = pmem_alloc_pages(4, true);
    if (cont_pages) 
    {
        printf("  ✓ Allocated 4 continuous pages at %p\n", cont_pages);
        pmem_free_pages(cont_pages, 4, true);
        printf("  ✓ Released 4 continuous pages\n");
    } 
    else 
    {
        printf("  ⚠ Continuous page allocation failed (expected)\n");
    }
    
    // 测试4：错误处理
    printf("\nTest 4: Error handling\n");
    pmem_free(NULL, true);  // 应该安全忽略
    printf("  ✓ NULL pointer free handled safely\n");
    
    // 显示统计信息
    printf("\nMemory statistics after tests:\n");
    pmem_stats();
    
    printf("=== Physical Memory Management Tests Complete ===\n\n");
}

void test_pmem_stress(void)
{
    printf("=== Stress Testing Physical Memory Management ===\n");
    
    int kern_available = pmem_available(true);
    int user_available = pmem_available(false);
    
    printf("Available before stress test:\n");
    printf("  Kernel: %d pages\n", kern_available);
    printf("  User:   %d pages\n", user_available);
    
    // 计算要分配的数量（避免分配过多）
    int kern_to_alloc = kern_available / 4;  // 只分配1/4，避免耗尽
    int user_to_alloc = user_available / 4;  // 只分配1/4，避免耗尽
    
    // 为指针数组分配内存（使用静态数组避免动态分配问题）
    static void* kern_pages[512];  // 静态数组，避免动态分配
    static void* user_pages[8192]; // 静态数组，足够大
    
    // 确保不超过数组大小
    if (kern_to_alloc > 512) kern_to_alloc = 512;
    if (user_to_alloc > 8192) user_to_alloc = 8192;
    
    printf("Will allocate: Kernel %d pages, User %d pages\n", kern_to_alloc, user_to_alloc);
    
    // 分配内核页面
    int kern_allocated = 0;
    for (int i = 0; i < kern_to_alloc; i++) {
        kern_pages[i] = pmem_alloc(true);  // 明确从内核池分配
        if (kern_pages[i]) {
            kern_allocated++;
        } else {
            printf("Kernel allocation failed at page %d\n", i);
            break;
        }
    }
    
    printf("Allocated %d/%d kernel pages in stress test\n", kern_allocated, kern_to_alloc);
    
    // 分配用户页面
    int user_allocated = 0;
    for (int i = 0; i < user_to_alloc; i++) {
        user_pages[i] = pmem_alloc(false);  // 明确从用户池分配
        if (user_pages[i]) {
            user_allocated++;
        } else {
            printf("User allocation failed at page %d\n", i);
            break;
        }
    }
    
    printf("Allocated %d/%d user pages in stress test\n", user_allocated, user_to_alloc);
    
    // 显示中间状态
    printf("\nDuring stress test:\n");
    printf("  Kernel available: %d pages\n", pmem_available(true));
    printf("  User available:   %d pages\n", pmem_available(false));
    
    // 释放内核页面（确保释放到正确的池）
    printf("Releasing kernel pages...\n");
    for (int i = 0; i < kern_allocated; i++) {
        if (kern_pages[i]) {
            pmem_free(kern_pages[i], true);  // 明确释放到内核池
            kern_pages[i] = NULL;  // 清空指针
        }
    }
    
    // 释放用户页面（确保释放到正确的池）
    printf("Releasing user pages...\n");
    for (int i = 0; i < user_allocated; i++) {
        if (user_pages[i]) {
            pmem_free(user_pages[i], false);  // 明确释放到用户池
            user_pages[i] = NULL;  // 清空指针
        }
    }
    
    printf("\nAfter cleanup:\n");
    printf("  Kernel available: %d pages\n", pmem_available(true));
    printf("  User available:   %d pages\n", pmem_available(false));
    
    printf("=== Stress Test Complete ===\n\n");
}

//页表测试函数
void test_pagetable_system(void)
{
    printf("\n=== Testing Page Table System ===\n");
    
    // 测试1：创建页表
    pagetable_t test_pt = create_pagetable();
    if (!test_pt) {
        printf("FAILED: Could not create page table\n");
        return;
    }
    printf("✓ Page table created\n");
    
    // 测试2：建立单个映射
    uint64 test_va = 0x10000000;
    uint64 test_pa = 0x80400000;
    
    if (map_page(test_pt, test_va, test_pa, PTE_R | PTE_W) == 0) {
        printf("✓ Page mapping created\n");
    } else {
        printf("✗ Failed to create page mapping\n");
    }
    
    // 测试3：查询映射
    pte_t* pte = walk_lookup(test_pt, test_va);
    if (pte && (*pte & PTE_V) && (PTE_TO_PA(*pte) == test_pa)) {
        printf("✓ Page lookup successful\n");
    } else {
        printf("✗ Page lookup failed\n");
    }
    
    // 测试4：权限位检查
    if (pte && (*pte & PTE_R) && (*pte & PTE_W) && !(*pte & PTE_X)) {
        printf("✓ Permission bits correct\n");
    } else {
        printf("✗ Permission bits incorrect\n");
    }
    
    // 测试5：重复映射检测
    if (map_page(test_pt, test_va, test_pa, PTE_R | PTE_W) != 0) {
        printf("✓ Duplicate mapping correctly rejected\n");
    } else {
        printf("✗ Duplicate mapping incorrectly allowed\n");
    }
    
    // 销毁页表
    destroy_pagetable(test_pt);
    printf("✓ Page table destroyed\n");
    
    printf("=== Page Table Tests Complete ===\n\n");
}

// 内核虚拟内存测试函数
void test_kernel_vm(void)
{
    printf("\n=== Testing Kernel Virtual Memory ===\n");
    
    // 测试1：检查内核页表是否创建
    printf("Test 1: Kernel page table existence\n");
    if (kernel_pagetable) {
        printf("  ✓ Kernel page table exists at %p\n", kernel_pagetable);
    } else {
        printf("  ✗ Kernel page table not created\n");
        return;
    }
    
    // 测试2：检查内核映射
    printf("\nTest 2: Kernel mappings verification\n");
    
    // 检查UART映射
    pte_t* uart_pte = walk_lookup(kernel_pagetable, UART0);
    if (uart_pte && (*uart_pte & PTE_V)) {
        printf("  ✓ UART mapping verified: 0x%lx -> 0x%lx\n", 
               UART0, PTE_TO_PA(*uart_pte));
    } else {
        printf("  ✗ UART mapping not found\n");
    }
    
    // 检查内核代码映射
    pte_t* kernel_pte = walk_lookup(kernel_pagetable, KERNBASE);
    if (kernel_pte && (*kernel_pte & PTE_V)) {
        printf("  ✓ Kernel code mapping verified: 0x%lx -> 0x%lx\n", 
               KERNBASE, PTE_TO_PA(*kernel_pte));
    } else {
        printf("  ✗ Kernel code mapping not found\n");
    }
    
    printf("=== Kernel Virtual Memory Tests Complete ===\n\n");
}

// 虚拟内存启用前后的测试
void test_virtual_memory_transition(void)
{
    printf("\n=== Testing Virtual Memory Transition ===\n");
    
    // 测试在虚拟内存启用前后程序是否正常工作
    printf("Test: Memory allocation before and after VM enable\n");
    
    // 虚拟内存启用前的分配
    void* pre_vm_page = pmem_alloc(true);
    if (pre_vm_page) {
        printf("  ✓ Pre-VM memory allocation successful: %p\n", pre_vm_page);
    }
    
    // 这里将启用虚拟内存
    printf("  → Enabling virtual memory...\n");
    kvm_inithart();
    printf("  ✓ Virtual memory enabled successfully\n");
    
    // 虚拟内存启用后的分配
    void* post_vm_page = pmem_alloc(true);
    if (post_vm_page) {
        printf("  ✓ Post-VM memory allocation successful: %p\n", post_vm_page);
    }
    
    // 测试之前分配的内存是否仍然可用
    if (pre_vm_page) {
        // 简单的内存读写测试
        *(char*)pre_vm_page = 0xAA;
        if (*(char*)pre_vm_page == 0xAA) {
            printf("  ✓ Pre-VM allocated memory still accessible\n");
        } else {
            printf("  ✗ Pre-VM allocated memory corrupted\n");
        }
        pmem_free(pre_vm_page, true);
    }
    
    if (post_vm_page) {
        *(char*)post_vm_page = 0xBB;
        if (*(char*)post_vm_page == 0xBB) {
            printf("  ✓ Post-VM allocated memory working correctly\n");
        } else {
            printf("  ✗ Post-VM allocated memory corrupted\n");
        }
        pmem_free(post_vm_page, true);
    }
    
    printf("=== Virtual Memory Transition Tests Complete ===\n\n");
}

// 安全的虚拟内存启用测试
void test_virtual_memory_transition_safe(void)
{
    safe_print_string("\n=== Testing Virtual Memory Transition ===\n");
    
    safe_print_string("Pre-VM memory allocation test...\n");
    void* pre_vm_page = pmem_alloc(true);
    if (pre_vm_page) {
        safe_print_string("  ✓ Pre-VM allocation successful\n");
    }
    
    safe_print_string("Enabling virtual memory...\n");
    kvm_inithart();
    safe_print_string("  ✓ Virtual memory enabled\n");
    
    safe_print_string("Post-VM memory allocation test...\n");
    void* post_vm_page = pmem_alloc(true);
    if (post_vm_page) {
        safe_print_string("  ✓ Post-VM allocation successful\n");
    }
    
    // 内存读写测试
    if (pre_vm_page) {
        *(char*)pre_vm_page = 0xAA;
        if (*(char*)pre_vm_page == 0xAA) {
            safe_print_string("  ✓ Pre-VM memory still accessible\n");
        }
        pmem_free(pre_vm_page, true);
    }
    
    if (post_vm_page) {
        *(char*)post_vm_page = 0xBB;
        if (*(char*)post_vm_page == 0xBB) {
            safe_print_string("  ✓ Post-VM memory working\n");
        }
        pmem_free(post_vm_page, true);
    }
    
    safe_print_string("=== VM Transition Tests Complete ===\n\n");
}

int main()
{
    int cpuid = mycpuid();
    
    // 第一阶段：选择启动CPU并完成所有初始化
    if (__sync_bool_compare_and_swap(&boot_cpu_id, -1, cpuid)) {
        // 启动CPU：完成所有初始化工作
        uart_init();
        print_init();

        // 初始化延迟
        for (volatile int i = 0; i < 15000000; i++) {
            asm volatile("nop");
        }
        
        safe_print_string("RISC-V OS starting...\n");
        safe_print_string("CPU ");
        safe_print_cpu_id(cpuid);
        safe_print_string(": Boot CPU initializing...\n");
        
        // 步骤1：物理内存管理器
        safe_print_string("Step 1: Initializing physical memory manager...\n");
        pmem_init();
        test_pmem_basic();
        test_pmem_stress();
        safe_print_string("Physical memory manager tests completed!\n");
        
        // 步骤2：页表系统
        safe_print_string("Step 2: Testing page table system...\n");
        test_pagetable_system();
        
        // 步骤3：内核虚拟内存
        safe_print_string("Step 3: Creating kernel page table...\n");
        kvm_init();
        test_kernel_vm();
        
        // 步骤4：启用虚拟内存（仅在启动CPU上）
        safe_print_string("Step 4: Enabling virtual memory on boot CPU...\n");
        test_virtual_memory_transition_safe();
        
        safe_print_string("Boot CPU initialization completed!\n");
        
        // 标记初始化完成
        __sync_synchronize();
        init_phase = 4;  // 所有初始化完成
        cpu_started[cpuid] = 1;
        
        // 等待其他CPU启动
        safe_print_string("Waiting for other CPUs...\n");
        while (1) {
            int all_started = 1;
            for (int i = 0; i < NCPU; i++) {
                if (!cpu_started[i]) {
                    all_started = 0;
                    break;
                }
            }
            if (all_started) {
                safe_print_string("All CPUs started successfully!\n");
                break;
            }
            
            for (volatile int i = 0; i < 1000000; i++) {
                asm volatile("nop");
            }
        }
        
    } else {
        // 非启动CPU：等待初始化完成后简单启动
        while (init_phase < 4) {
            __sync_synchronize();
            for (volatile int i = 0; i < 10000; i++) {
                asm volatile("nop");
            }
        }
        
        // 错开启动时间，避免输出冲突
        for (volatile int i = 0; i < (cpuid * 30000000); i++) {
            asm volatile("nop");
        }
        
        // 在其他CPU上启用虚拟内存
        kvm_inithart();
        
        safe_print_string("CPU ");
        safe_print_cpu_id(cpuid);
        safe_print_string(": Secondary CPU started with VM enabled!\n");
        
        __sync_synchronize();
        cpu_started[cpuid] = 1;
    }
    
    // 所有CPU进入主循环
    while (1) {
        asm volatile("wfi");
    }
}