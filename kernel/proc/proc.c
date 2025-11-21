// kernel/proc/proc.c
#include "lib/print.h"
#include "mem/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/kvm.h"
#include "proc/cpu.h"
#include "proc/proc.h"
#include "trap/trap.h"
#include "memlayout.h"
#include "riscv.h"
#include "common.h"
#include "fs/file.h"

#define USE_SIMPLE_TEST       // 简单测试（两次fork）
//#define USE_INITCODE          // 基础测试（一次fork）- 默认

#ifdef USE_SIMPLE_TEST
    #include "simple_test.h"
    #define CURRENT_PROGRAM simple_test_bin
    #define CURRENT_PROGRAM_LEN simple_test_bin_len
#else
    // 默认使用 initcode（任务一的基础测试）
    #include "initcode.h"
    #define CURRENT_PROGRAM initcode_bin
    #define CURRENT_PROGRAM_LEN initcode_bin_len
#endif

static void init_process_standard_files(proc_t* p);

//static uint64 saved_return_address = 0;

// ===== 全局变量 =====
proc_t proc_table[MAX_PROC];            // 进程表
proc_t proczero;                        // 第一个进程
proc_t *initproc;                       // init进程指针
struct spinlock wait_lock;              // wait系统调用锁
static spinlock_t proc_table_lock;      // 进程表锁
static spinlock_t pid_lock;             // PID分配锁
static int next_pid = 2;                // 下一个可用PID（从2开始，因为1已经被init使用）

// ===== 前向声明 =====
static int alloc_pid(void);
static void free_pid(int pid);

static inline int min(int a, int b) {
    return (a < b) ? a : b;
}

// ===== 进程初始化 =====
void proc_init(void) {
    printf("Initializing process management system...\n");
    
    debug_context_size();

    // 初始化锁
    spinlock_init(&proc_table_lock, "proc_table");
    spinlock_init(&pid_lock, "pid_alloc");
    spinlock_init(&wait_lock, "wait_lock"); 

    // 初始化进程表
    for (int i = 0; i < MAX_PROC; i++) {
        spinlock_init(&proc_table[i].lock, "proc");
        proc_table[i].pid = 0;
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pgtbl = NULL;
        proc_table[i].heap_top = 0;
        proc_table[i].ustack_pages = 0;
        proc_table[i].tf = NULL;
        proc_table[i].kstack = 0;
        memset(&proc_table[i].ctx, 0, sizeof(context_t));
        proc_table[i].wait_chan = NULL;
        proc_table[i].exit_code = 0;
        proc_table[i].killed = 0;
        proc_table[i].parent = NULL;
        proc_table[i].next = NULL;
        memset(proc_table[i].name, 0, sizeof(proc_table[i].name));
        proc_table[i].privilege_level = 0;
        
        // 初始化文件描述符表
        for (int j = 0; j < NOFILE; j++) {
            proc_table[i].ofile[j] = NULL;
        }
    }
    
    // ✅ 修复：先清零，再初始化锁和字段
    memset(&proczero, 0, sizeof(proc_t));
    spinlock_init(&proczero.lock, "proczero");  // 在 memset 之后初始化锁
    
    proczero.pid = 0;
    proczero.state = PROC_RUNNING;
    strcpy(proczero.name, "proczero");
    proczero.privilege_level = PRIVILEGE_KERNEL;
    proczero.heap_top = USER_HEAP_BASE;
    proczero.ustack_pages = 1;
    
    // 初始化 proczero 的文件描述符表
    for (int i = 0; i < NOFILE; i++) {
        proczero.ofile[i] = NULL;
    }
    
    printf("Process management system initialized\n");
    printf("Next available PID: %d\n", next_pid);
}

// ===== PID管理函数 =====

// PID分配
static int alloc_pid(void) {
    spinlock_acquire(&pid_lock);
    
    int pid = next_pid;
    
    // 简单的PID分配策略：线性递增
    if (next_pid >= 32767) {  // 避免PID过大
        printf("alloc_pid: PID space exhausted\n");
        spinlock_release(&pid_lock);
        return -1;
    }
    
    next_pid++;
    
    spinlock_release(&pid_lock);
    
    printf("alloc_pid: allocated PID=%d\n", pid);
    return pid;
}

// PID释放
static void free_pid(int pid) {
    spinlock_acquire(&pid_lock);
    
    // 简单实现：不回收PID
    printf("free_pid: freed PID=%d (not recycled)\n", pid);
    
    spinlock_release(&pid_lock);
}

// ===== proc_alloc() 函数 =====
proc_t* proc_alloc(void) {
    printf("proc_alloc: allocating new process\n");
    
    spinlock_acquire(&proc_table_lock);
    
    // 寻找空闲进程槽
    proc_t* p = NULL;
    for (int i = 0; i < MAX_PROC; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            p = &proc_table[i];
            p->state = PROC_EMBRYO;  // 立即标记为正在创建，防止竞争
            break;
        }
    }
    
    spinlock_release(&proc_table_lock);
    
    if (!p) {
        printf("proc_alloc: no free process slots\n");
        return NULL;
    }
    
    // 分配PID
    p->pid = alloc_pid();
    if (p->pid < 0) {
        printf("proc_alloc: failed to allocate PID\n");
        p->state = PROC_UNUSED;
        return NULL;
    }
    
    printf("proc_alloc: allocated PID=%d\n", p->pid);
    
    // 设置基本信息
    p->privilege_level = PRIVILEGE_USER;
    strcpy(p->name, "new_proc");  // 默认名称
    
    // 分配内核栈
    void* kstack_page = pmem_alloc(true);  // 分配内核页
    if (!kstack_page) {
        printf("proc_alloc: failed to allocate kernel stack\n");
        free_pid(p->pid);
        p->state = PROC_UNUSED;
        return NULL;
    }
    p->kstack = (uint64)kstack_page;  // 栈底地址
    printf("proc_alloc: allocated kernel stack at 0x%lx (bottom)\n", p->kstack);
    
    // 分配trapframe
    p->tf = (trapframe_t*)pmem_alloc(false);  // ✅ 使用用户页面
    if (!p->tf) {
        printf("proc_alloc: failed to allocate trapframe\n");
        pmem_free((uint64)kstack_page, true);
        free_pid(p->pid);
        p->state = PROC_UNUSED;
        return NULL;
    }
    memset(p->tf, 0, sizeof(trapframe_t));
    printf("proc_alloc: allocated trapframe at 0x%lx\n", (uint64)p->tf);
    
    // 创建用户页表
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (!p->pgtbl) {
        printf("proc_alloc: failed to create user page table\n");
        pmem_free((uint64)p->tf, false);
        pmem_free((uint64)kstack_page, true);
        free_pid(p->pid);
        p->state = PROC_UNUSED;
        return NULL;
    }
    printf("proc_alloc: created user page table at 0x%lx\n", (uint64)p->pgtbl);
    
    // 初始化其他字段
    p->parent = NULL;
    p->children = NULL;
    p->sibling = NULL;
    p->exit_code = 0;
    p->killed = 0;
    
    // 初始化文件描述符表
    for (int i = 0; i < NOFILE; i++) {
        p->ofile[i] = NULL;
    }
    p->cwd = NULL;
    
    // 初始化内存管理
    p->heap_top = USER_HEAP_BASE;  // 设置初始堆顶
    p->ustack_pages = 1;           // 初始栈页数
    
    // 初始化同步字段
    memset(&p->ctx, 0, sizeof(context_t));
    p->wait_chan = NULL;
    p->killed = 0;
    p->next = NULL;
    
    // 初始化信号字段
    p->pending_signals = 0;
    p->signal_mask = 0;
    
    printf("proc_alloc: successfully allocated process PID=%d\n", p->pid);
    printf("proc_alloc: initialized parent pointer to 0x%lx\n", (uint64)p->parent);
    
    return p;
}

// ===== proc_free() 函数 =====
void proc_free(proc_t* p) {
    if (!p) {
        printf("proc_free: null pointer\n");
        return;
    }
    
    printf("proc_free: freeing process PID=%d (%s)\n", p->pid, p->name);
    
    // ✅ 1. 释放用户页表和相关内存
    if (p->pgtbl && p->pgtbl != NULL) {
        printf("proc_free: freeing user page table and memory\n");
        
        // 释放用户内存页面
        proc_free_memory(p);
        
        // 释放页表本身
        proc_freepagetable(p->pgtbl, p->heap_top);
        p->pgtbl = NULL;
    }
    
    // ✅ 2. 释放trapframe
    if (p->tf) {
        printf("proc_free: freeing trapframe\n");
        pmem_free((uint64)p->tf, false);  // trapframe 使用用户页面
        p->tf = NULL;
    }
    
    // ✅ 3. 释放内核栈
    if (p->kstack && p->kstack != 0) {
        printf("proc_free: freeing kernel stack\n");
        pmem_free(p->kstack, true);  // ✅ 修复：直接释放栈底地址
        p->kstack = 0;
    }
    
    // ✅ 4. 关闭所有文件描述符
    printf("proc_free: closing file descriptors\n");
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i]) {
            printf("proc_free: closing fd %d\n", i);
            fileclose(p->ofile[i]);  // ✅ 实现：关闭文件，减少引用计数
            p->ofile[i] = NULL;
        }
    }
    
    // ✅ 5. 释放当前工作目录
    if (p->cwd) {
        printf("proc_free: releasing current working directory\n");
        iput(p->cwd);  // ✅ 实现：释放inode引用
        p->cwd = NULL;
    }
    
    // 释放PID
    if (p->pid > 0) {
        free_pid(p->pid);
    }
    
    // 清空基本信息
    p->pid = 0;
    p->state = PROC_UNUSED;
    memset(p->name, 0, sizeof(p->name));
    p->privilege_level = 0;
    
    // 清空进程关系
    p->parent = NULL;
    p->children = NULL;
    p->sibling = NULL;
    p->exit_code = 0;
    
    // 清空内存管理字段
    p->heap_top = 0;
    p->ustack_pages = 0;
    
    // 清空同步字段
    memset(&p->ctx, 0, sizeof(context_t));
    p->wait_chan = NULL;
    p->killed = 0;
    p->next = NULL;
    
    // 清空信号字段
    p->pending_signals = 0;
    p->signal_mask = 0;
    
    printf("proc_free: process freed successfully\n");
}

void debug_page_table_mapping(pgtbl_t pgtbl, uint64 va) {
    printf("Debug mapping for VA=0x%lx:\n", va);
    
    pte_t *pte = walk_lookup(pgtbl, va);
    if (pte && (*pte & PTE_V)) {
        uint64 pa = PTE_TO_PA(*pte);
        printf("  Mapped to PA=0x%lx\n", pa);
        printf("  Flags: R=%d W=%d X=%d U=%d\n",
               (*pte & PTE_R) ? 1 : 0,
               (*pte & PTE_W) ? 1 : 0,
               (*pte & PTE_X) ? 1 : 0,
               (*pte & PTE_U) ? 1 : 0);
    } else {
        printf("  Not mapped!\n");
    }
}

// ===== 创建用户页表 =====
pgtbl_t proc_pgtbl_init(uint64 trapframe_pa) {
    printf("Creating user page table...\n");
    
    extern pagetable_t kernel_pagetable;
    
    pgtbl_t pgtbl = create_pagetable();
    if (!pgtbl) {
        printf("Failed to create user page table\n");
        return 0;
    }
    
    uint64* kernel_pte = (uint64*)kernel_pagetable;
    uint64* user_pte = (uint64*)pgtbl;
    
    // ✅ 只复制内核高地址映射（索引 256-511）
    // 但是跳过 TRAMPOLINE/TRAPFRAME 所在的索引
    printf("Copying kernel mappings (high half)...\n");
    
    // 计算 TRAMPOLINE 和 TRAPFRAME 的 Level-2 索引
    uint64 trampoline_idx = (TRAMPOLINE >> 30) & 0x1FF;
    uint64 trapframe_idx = (TRAPFRAME >> 30) & 0x1FF;
    
    printf("TRAMPOLINE L2 index: %ld, TRAPFRAME L2 index: %ld\n", 
           trampoline_idx, trapframe_idx);
    
    for (int i = 256; i < 512; i++) {
        // ✅ 跳过 TRAMPOLINE/TRAPFRAME 的索引，不复制
        if (i == trampoline_idx) {
            printf("Skipping index %d (TRAMPOLINE/TRAPFRAME)\n", i);
            continue;
        }
        
        if (kernel_pte[i] & PTE_V) {
            user_pte[i] = kernel_pte[i];
        }
    }
    
    // ✅ 手动映射 TRAMPOLINE（从内核页表中获取物理地址）
    extern char trampoline[];
    uint64 trampoline_pa = (uint64)trampoline;
    
    printf("Mapping trampoline: va=0x%lx -> pa=0x%lx\n", TRAMPOLINE, trampoline_pa);
    
    if (map_page(pgtbl, TRAMPOLINE, trampoline_pa, PTE_R | PTE_X) != 0) {
        printf("Failed to map trampoline\n");
        destroy_pagetable(pgtbl);
        return 0;
    }
    
    // ✅ 手动映射 TRAPFRAME（使用传入的物理地址）
    printf("Mapping trapframe: va=0x%lx -> pa=0x%lx\n", TRAPFRAME, trapframe_pa);
    
    if (map_page(pgtbl, TRAPFRAME, trapframe_pa, PTE_R | PTE_W) != 0) {
        printf("Failed to map trapframe\n");
        destroy_pagetable(pgtbl);
        return 0;
    }
    
    printf("User page table created successfully\n");

    debug_page_table_mapping(pgtbl, TRAMPOLINE);
    debug_page_table_mapping(pgtbl, TRAPFRAME);

    return pgtbl;
}

// 创建第一个用户进程
void proc_make_first(){
    printf("=== Creating First User Process ===\n");
    
    proc_t* p = proc_alloc();
    if (!p) {
        panic("proc_make_first: failed to allocate first process");
    }

    strcpy(p->name, "init");
    proczero.children = p;

    initproc = p;

    printf("Process name set to: %s (PID=%d)\n", p->name, p->pid);

    printf("Using pre-allocated resources:\n");
    printf("  Trapframe: 0x%lx\n", (uint64)p->tf);
    printf("  Kernel stack: 0x%lx\n", p->kstack);
    printf("  Page table: 0x%lx\n", (uint64)p->pgtbl);
    
    // === 映射内核栈到内核页表 ===
    extern pagetable_t kernel_pagetable;
    uint64 kstack_va = KSTACK(0);
    uint64 kstack_pa = p->kstack;  // 物理地址是栈底
    
    printf("Mapping kernel stack in kernel page table:\n");
    printf("  VA: 0x%lx -> PA: 0x%lx\n", kstack_va, kstack_pa);
    
    if (map_page(kernel_pagetable, kstack_va, kstack_pa, PTE_R | PTE_W) != 0) {
        panic("failed to map kernel stack");
    }
    printf("Kernel stack mapped successfully\n");
    
    // === 6. 加载用户程序 ===
    uint64 code_va = 0x1000;

    //使用新的宏定义
    uint32 program_size = CURRENT_PROGRAM_LEN;
    printf("Program size: %d bytes, pages needed: ", program_size);

    uint32 pages_needed = (program_size + PGSIZE - 1) / PGSIZE;  // 向上取整
    printf("%d\n", pages_needed);

    //分配多个页面
    for (uint32 i = 0; i < pages_needed; i++) {
        uint64 code_pa = (uint64)pmem_alloc(false);
        if (!code_pa) panic("failed to allocate code page");
        
        printf("Loading program page %d to PA: 0x%lx\n", i, code_pa);
        memset((void*)code_pa, 0, PGSIZE);  // 确保整页都清零
        
        // 复制程序数据到这个页面
        uint32 copy_size = PGSIZE;
        uint32 offset = i * PGSIZE;
        
        if (offset < program_size) {
            if (offset + PGSIZE > program_size) {
                copy_size = program_size - offset;
            }
            
            // 使用新的宏定义
            memcpy((void*)code_pa, CURRENT_PROGRAM + offset, copy_size);
            printf("Copied %d bytes to page %d (offset %d)\n", copy_size, i, offset);
        }
        
        // 对于第2页，确保剩余部分可以用作数据段
        if (i == 1) {
            printf("Page 1 will serve as code+data segment\n");
        }
        
        asm volatile("fence" ::: "memory");
        asm volatile("fence.i" ::: "memory");
        
        uint64 page_va = code_va + i * PGSIZE;
        uint64 pte_flags;
        if (i == 0) {
            pte_flags = PTE_R | PTE_X | PTE_U;  // 第一页：只读+可执行
        } else {
            pte_flags = PTE_R | PTE_W | PTE_X | PTE_U;  // 后续页：可读写执行
        }
        
        if (map_page(p->pgtbl, page_va, code_pa, pte_flags) != 0) {
            panic("failed to map user code page");
        }
        printf("User code page %d mapped: VA=0x%lx -> PA=0x%lx (flags=0x%lx)\n", 
               i, page_va, code_pa, pte_flags);
    }

    // 设置堆起始地址在代码页之后
    p->heap_top = code_va + pages_needed * PGSIZE;
    printf("Heap initialized at: 0x%lx\n", p->heap_top);
    printf("Total program loaded: %d bytes in %d pages\n", program_size, pages_needed);

    // === 7. 用户栈映射 ===
    // 栈地址要避开堆空间
    uint64 code_end_va = 0x1000 + pages_needed * PGSIZE;  // 代码结束地址
    uint64 heap_start = code_end_va;                       // 堆紧跟代码
    uint64 ustack_va = 0x10000;                           // 栈放在更远的地方（64KB处）

    printf("Code ends at: 0x%lx, heap starts at: 0x%lx, stack at: 0x%lx\n", 
           code_end_va, heap_start, ustack_va);

    uint64 ustack_pa = (uint64)pmem_alloc(false);
    if (!ustack_pa) panic("failed to allocate user stack");

    memset((void*)ustack_pa, 0, PGSIZE);

    if (map_page(p->pgtbl, ustack_va, ustack_pa, PTE_R | PTE_W | PTE_U) != 0) {
        panic("failed to map user stack");
    }
    printf("User stack mapped: VA=0x%lx -> PA=0x%lx\n", ustack_va, ustack_pa);

    // === 8. 配置 trapframe ===
    p->tf->kernel_satp = MAKE_SATP(kernel_pagetable);
    p->tf->kernel_sp = p->kstack + PGSIZE; 
    p->tf->kernel_trap = (uint64)trap_user_handler;

    p->tf->epc = 0x1000;
    p->tf->sp = ustack_va + PGSIZE - 8;  // 使用新的栈地址

    // 设置正确的堆起始地址
    p->heap_top = heap_start;

    printf("Trapframe configured:\n");
    printf("  epc: 0x%lx (user program entry)\n", p->tf->epc);
    printf("  sp: 0x%lx (user stack top)\n", p->tf->sp);
    printf("  heap_top: 0x%lx\n", p->heap_top);

    // 清零所有用户寄存器
    p->tf->ra = 0;
    p->tf->gp = 0;
    p->tf->tp = 0;
    p->tf->t0 = 0;
    p->tf->t1 = 0;
    p->tf->t2 = 0;
    p->tf->s0 = 0;
    p->tf->s1 = 0;
    p->tf->a0 = 0;
    p->tf->a1 = 0;
    p->tf->a2 = 0;
    p->tf->a3 = 0;
    p->tf->a4 = 0;
    p->tf->a5 = 0;
    p->tf->a6 = 0;
    p->tf->a7 = 0;
    p->tf->s2 = 0;
    p->tf->s3 = 0;
    p->tf->s4 = 0;
    p->tf->s5 = 0;
    p->tf->s6 = 0;
    p->tf->s7 = 0;
    p->tf->s8 = 0;
    p->tf->s9 = 0;
    p->tf->s10 = 0;
    p->tf->s11 = 0;
    p->tf->t3 = 0;
    p->tf->t4 = 0;
    p->tf->t5 = 0;
    p->tf->t6 = 0;

    // === 9. 初始化文件描述符 ===
    printf("Initializing standard file descriptors...\n");
    init_process_standard_files(p);

    // === 10. 切换到用户模式 ===
    p->state = PROC_RUNNABLE;

    printf("About to switch to user program at 0x%lx\n", p->tf->epc);
    
    // 添加更多调试信息
    printf("Debug: User program details:\n");
    printf("  EPC: 0x%lx\n", p->tf->epc);
    printf("  SP: 0x%lx\n", p->tf->sp);
    printf("  SATP: 0x%lx\n", p->tf->kernel_satp);
    printf("  Page table: 0x%lx\n", (uint64)p->pgtbl);
    
    printf("Starting scheduler...\n");
    
    //scheduler(); 
    
    //panic("scheduler returned - this should never happen");
}

// ===== 初始化进程标准文件描述符 =====
void init_process_standard_files(proc_t* p)
{
    printf("Initializing standard file descriptors...\n");
    
    // 清空文件描述符表
    for (int i = 0; i < NOFILE; i++) {
        p->ofile[i] = NULL;
    }
    
    // 正确初始化标准文件描述符
    for (int fd = 0; fd < 3; fd++) {
        struct file *f = filealloc();
        if (!f) {
            printf("ERROR: Failed to allocate file for fd %d\n", fd);
            panic("Cannot initialize standard files");
        }
        
        // 确保所有字段都正确设置
        f->type = FD_DEVICE_E;
        f->major = CONSOLE;
        f->readable = (fd == 0) ? 1 : 0;  // stdin 可读
        f->writable = (fd == 0) ? 0 : 1;  // stdout/stderr 可写
        f->ref = 1;
        f->off = 0;  // 设置文件偏移
        
        // 确保设备文件有正确的操作函数
        // 这通常在设备初始化时设置，但我们需要确保它存在
        
        p->ofile[fd] = f;
        printf("✅ Initialized fd %d: type=%d, major=%d, readable=%d, writable=%d, ref=%d\n", 
               fd, f->type, f->major, f->readable, f->writable, f->ref);
    }
    
    printf("Standard file descriptors initialized successfully\n");
}

// 进程管理函数
int proc_copy_memory(proc_t *parent, proc_t *child) {
    printf("proc_copy_memory: copying memory from PID=%d to PID=%d\n", 
           parent->pid, child->pid);
    
    // 1. 复制代码页（从 0x1000 到 heap_top）
    printf("proc_copy_memory: copying code pages from 0x%lx to 0x%lx\n", 
           0x1000UL, parent->heap_top);
    
    for (uint64 va = 0x1000; va < parent->heap_top; va += PGSIZE) {
        // 获取父进程页面的物理地址
        uint64 parent_pa = va_to_pa(parent->pgtbl, va);
        if (parent_pa == 0) {
            printf("proc_copy_memory: parent page VA=0x%lx not mapped, skipping\n", va);
            continue;
        }
        
        // 为子进程分配新的物理页
        uint64 child_pa = (uint64)pmem_alloc(false);
        if (!child_pa) {
            printf("proc_copy_memory: failed to allocate page for VA=0x%lx\n", va);
            return -1;
        }
        
        // 复制页面内容
        memcpy((void*)child_pa, (void*)parent_pa, PGSIZE);
        
        // 确定页面权限（代码段第一页是只读+可执行，其他是读写执行）
        uint64 flags;
        if (va == 0x1000) {
            flags = PTE_R | PTE_X | PTE_U;  // 第一页：只读+可执行
        } else {
            flags = PTE_R | PTE_W | PTE_X | PTE_U;  // 其他页：可读写执行
        }
        
        // 映射到子进程页表
        if (map_page(child->pgtbl, va, child_pa, flags) != 0) {
            printf("proc_copy_memory: failed to map child page at VA=0x%lx\n", va);
            pmem_free(child_pa, false);
            return -1;
        }
        
        printf("proc_copy_memory: copied page VA=0x%lx (parent_pa=0x%lx -> child_pa=0x%lx)\n",
               va, parent_pa, child_pa);
    }
    
    // 2. 复制用户栈（假设栈在 0x10000）
    uint64 ustack_va = 0x10000;
    printf("proc_copy_memory: copying user stack at VA=0x%lx\n", ustack_va);
    
    uint64 parent_pa = va_to_pa(parent->pgtbl, ustack_va);
    if (parent_pa != 0) {
        // 为子进程分配栈页
        uint64 child_pa = (uint64)pmem_alloc(false);
        if (!child_pa) {
            printf("proc_copy_memory: failed to allocate stack page\n");
            return -1;
        }
        
        // 复制栈内容
        memcpy((void*)child_pa, (void*)parent_pa, PGSIZE);
        
        // 映射到子进程页表
        if (map_page(child->pgtbl, ustack_va, child_pa, PTE_R | PTE_W | PTE_U) != 0) {
            printf("proc_copy_memory: failed to map stack page\n");
            pmem_free(child_pa, false);
            return -1;
        }
        
        printf("proc_copy_memory: copied stack VA=0x%lx (parent_pa=0x%lx -> child_pa=0x%lx)\n",
               ustack_va, parent_pa, child_pa);
    }
    
    // 3. 复制元数据
    child->heap_top = parent->heap_top;
    child->ustack_pages = parent->ustack_pages;
    
    printf("proc_copy_memory: successfully copied all memory\n");
    printf("  heap_top: 0x%lx\n", child->heap_top);
    printf("  ustack_pages: %d\n", child->ustack_pages);
    
    return 0;
}

void proc_copy_files(proc_t *parent, proc_t *child) {
    printf("proc_copy_files: copying files from PID=%d to PID=%d\n", 
           parent->pid, child->pid);
    
    for (int i = 0; i < NOFILE; i++) {
        if (parent->ofile[i]) {
            child->ofile[i] = filedup(parent->ofile[i]);
            printf("proc_copy_files: copied fd %d\n", i);
        }
    }
    
    if (parent->cwd) {
        child->cwd = idup(parent->cwd);
    }
}

void proc_free_memory(proc_t *p) {
    printf("proc_free_memory: freeing memory for PID=%d\n", p->pid);
    
    if (!p->pgtbl) {
        printf("proc_free_memory: no page table to free\n");
        return;
    }
    
    // ✅ 使用现有的 va_to_pa 函数
    // 释放代码段页面（从 0x1000 开始）
    uint64 code_start = 0x1000;
    uint64 code_end = p->heap_top;
    
    printf("proc_free_memory: freeing code pages from 0x%lx to 0x%lx\n", 
           code_start, code_end);
    
    for (uint64 va = code_start; va < code_end; va += PGSIZE) {
        uint64 pa = va_to_pa(p->pgtbl, va);  // ✅ 使用现有函数
        if (pa != 0) {
            printf("proc_free_memory: freeing page VA=0x%lx, PA=0x%lx\n", va, pa);
            pmem_free(pa, false);  // 释放用户页面
            
            // ✅ 使用现有的 unmap_page 函数
            unmap_page(p->pgtbl, va);
        }
    }
    
    // 释放用户栈页面
    uint64 ustack_va = 0x10000;  // 根据你的代码中的栈地址
    uint64 pa = va_to_pa(p->pgtbl, ustack_va);  // ✅ 使用现有函数
    if (pa != 0) {
        printf("proc_free_memory: freeing user stack VA=0x%lx, PA=0x%lx\n", 
               ustack_va, pa);
        pmem_free(pa, false);
        unmap_page(p->pgtbl, ustack_va);  // ✅ 使用现有函数
    }
    
    printf("proc_free_memory: memory freed\n");
}

proc_t* find_child(proc_t *parent, int pid) {
    for (proc_t *p = proc_table; p < &proc_table[MAX_PROC]; p++) {
        if (p->parent == parent && (pid == -1 || p->pid == pid)) {
            return p;
        }
    }
    return 0;
}

// proc.c - 修改 wakeup 函数，添加死锁检测
void wakeup(void *chan) {
    proc_t *p;
    
    for (p = proc_table; p < &proc_table[MAX_PROC]; p++) {
        if (p != myproc()) {
            spinlock_acquire(&p->lock);
            if (p->state == PROC_SLEEPING && p->wait_chan == chan) {
                p->state = PROC_RUNNABLE;
            }
            spinlock_release(&p->lock);
        }
    }
}

__attribute__((noinline))
void sched(void) {
    proc_t *p = myproc();  // ✅ 在 swtch 之前调用
    
    if (!p) panic("sched: no current process");
    if (!spinlock_holding(&p->lock)) panic("sched: not holding lock");
    if (p->state == PROC_RUNNING) panic("sched: still running");
    
    cpu_t *cpu = mycpu();  // ✅ 在 swtch 之前调用
    int intena = cpu->intena;
    int pid = p->pid;  // ✅ 保存 PID 到局部变量
    
    printf("sched: [BEFORE SWTCH] PID=%d\n", pid);
    
    swtch(&p->ctx, &cpu->ctx);
    
    // ✅ swtch 返回后，不要调用 myproc() 或 mycpu()
    // 使用之前保存的指针和变量
    printf("sched: [AFTER SWTCH] PID=%d RETURNED!\n", pid);  // ✅ 使用局部变量
    
    cpu->intena = intena;  // ✅ 使用之前保存的指针
    
    printf("sched: [COMPLETE] PID=%d\n", pid);  // ✅ 使用局部变量
}

__attribute__((noinline))
void sleep(void *chan, struct spinlock *lk) {
    proc_t *p = myproc();

    // ✅ 检查栈指针
    uint64 current_sp;
    asm volatile("mv %0, sp" : "=r"(current_sp));
    
    uint64 expected_sp_min = p->kstack;
    uint64 expected_sp_max = p->kstack + PGSIZE;
    
    printf("sleep: [ENTRY] PID=%d, sp=0x%lx\n", p->pid, current_sp);
    printf("sleep: expected sp range: 0x%lx - 0x%lx\n", 
           expected_sp_min, expected_sp_max);
    
    if (current_sp < expected_sp_min || current_sp > expected_sp_max) {
        printf("ERROR: stack pointer out of range!\n");
        printf("  Fixing: setting sp to 0x%lx\n", expected_sp_max - 16);
        
        // 强制修正栈指针
        asm volatile("mv sp, %0" :: "r"(expected_sp_max - 16));
    }
    
    printf("sleep: [START] PID=%d on chan=0x%lx\n", p->pid, (uint64)chan);
    
    spinlock_acquire(&p->lock);
    
    if (lk != &p->lock) {
        spinlock_release(lk);
    }
    
    p->wait_chan = chan;
    p->state = PROC_SLEEPING;
    
    printf("sleep: [CALLING SCHED]\n");
    sched();
    
    printf("sleep: [WOKE UP] - sched() returned!\n");
    
    p->wait_chan = 0;
    
    if (lk != &p->lock) {
        spinlock_acquire(lk);
    }
    
    spinlock_release(&p->lock);
    
    printf("sleep: [COMPLETE]\n");
}


// ✅ 添加：释放进程页表的函数
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    printf("proc_freepagetable: freeing pagetable at 0x%lx (size=0x%lx)\n", 
           (uint64)pagetable, sz);
    
    if (!pagetable) {
        printf("proc_freepagetable: null pagetable\n");
        return;
    }
    
    // 使用现有的 destroy_pagetable 函数
    destroy_pagetable(pagetable);
    
    printf("proc_freepagetable: pagetable freed\n");
}

void forkret(void) {
    static int first = 1;
    proc_t *p = myproc();
    
    printf("forkret: ENTRY - PID=%d\n", p ? p->pid : -1);
    
    // ✅ 释放进程锁（调度器持有）
    spinlock_release(&p->lock);
    
    printf("forkret: lock released\n");
    
    if (first) {
        first = 0;
        printf("forkret: first time initialization\n");
    }
    
    printf("forkret: about to call trap_user_return()\n");
    
    // ✅ 跳转到用户态
    trap_user_return();
    
    panic("forkret: trap_user_return returned!");
}

void scheduler(void) {
    cpu_t *c = mycpu();
    c->proc = 0;
    
    printf("Scheduler started on CPU %d\n", mycpuid());
    
    for(;;) {
        intr_on();
        
        int found = 0;
        int total_procs = 0;     // ✅ 总进程数
        int zombie_procs = 0;    // ✅ 僵尸进程数
        
        for (proc_t *p = proc_table; p < &proc_table[MAX_PROC]; p++) {
            spinlock_acquire(&p->lock);
            
            // ✅ 统计进程
            if (p->state != PROC_UNUSED) {
                total_procs++;
                if (p->state == PROC_ZOMBIE) {
                    zombie_procs++;
                }
            }
            
            if (p->state == PROC_RUNNABLE) {
                printf("Scheduler: found runnable PID=%d (%s)\n", p->pid, p->name);
                
                if (p->ctx.sp == 0) {
                    p->ctx.sp = p->kstack + PGSIZE;
                    p->ctx.ra = (uint64)forkret;
                    printf("Scheduler: FIRST TIME - ctx.ra=0x%lx, ctx.sp=0x%lx\n", 
                           p->ctx.ra, p->ctx.sp);
                } else {
                    printf("Scheduler: RESUMING - ctx.ra=0x%lx, ctx.sp=0x%lx\n", 
                           p->ctx.ra, p->ctx.sp);
                }
                
                p->state = PROC_RUNNING;
                c->proc = p;
                
                printf("Scheduler: [BEFORE SWTCH] to PID=%d\n", p->pid);
                printf("  c->ctx.ra = 0x%lx\n", c->ctx.ra);
                printf("  c->ctx.sp = 0x%lx\n", c->ctx.sp);
                printf("  p->ctx.ra = 0x%lx\n", p->ctx.ra);
                printf("  p->ctx.sp = 0x%lx\n", p->ctx.sp);
                
                swtch(&c->ctx, &p->ctx);
                
                printf("Scheduler: [AFTER SWTCH] from PID=%d\n", p->pid);
                
                c->proc = 0;
                found = 1;
            }
            
            spinlock_release(&p->lock);
        }
        
        // ✅ 检查是否所有进程都是僵尸
        if (found == 0 && total_procs > 0 && total_procs == zombie_procs) {
            printf("\n");
            printf("========================================\n");
            printf("✅ All processes have completed!\n");
            printf("========================================\n");
            printf("Total processes: %d (all zombies)\n", total_procs);
            printf("\n");
            
            // 打印最终统计
            printf("=== Final System State ===\n");
            for (proc_t *p = proc_table; p < &proc_table[MAX_PROC]; p++) {
                if (p->state != PROC_UNUSED) {
                    printf("  PID=%d (%s): state=%d", p->pid, p->name, p->state);
                    if (p->state == PROC_ZOMBIE) {
                        printf(" [ZOMBIE, exit_code=%d]", p->exit_code);
                    }
                    printf("\n");
                }
            }
            printf("==========================\n");
            printf("\n");
            
            // ✅ 关闭 QEMU
            printf("Shutting down QEMU...\n");
            volatile uint32_t *test_dev = (uint32_t*)0x100000;
            *test_dev = 0x5555;  // QEMU shutdown code
            
            // 如果关闭失败，无限循环
            printf("Shutdown failed, entering infinite loop\n");
            for(;;) {
                asm volatile("wfi");
            }
        }
        
        if (found == 0) {
            intr_on();
            asm volatile("wfi");
        }
    }
}

void validate_stack_pointer(proc_t *p) {
    uint64 expected_sp_min = KSTACK(0);
    uint64 expected_sp_max = KSTACK(0) + PGSIZE;
    
    printf("validate_stack_pointer: PID=%d\n", p->pid);
    printf("  ctx.sp: 0x%lx\n", p->ctx.sp);
    printf("  expected range: 0x%lx - 0x%lx\n", expected_sp_min, expected_sp_max);
    
    if (p->ctx.sp < expected_sp_min || p->ctx.sp > expected_sp_max) {
        printf("ERROR: stack pointer 0x%lx out of range!\n", p->ctx.sp);
        printf("  fixing ctx.sp to 0x%lx\n", expected_sp_max);
        p->ctx.sp = expected_sp_max;  // 强制修复
    } else {
        printf("✅ stack pointer is valid\n");
    }
}

void check_stack_usage(const char* location) {
    uint64 current_sp;
    asm volatile("mv %0, sp" : "=r" (current_sp));
    
    uint64 stack_bottom = KSTACK(0);
    uint64 stack_top = KSTACK(0) + PGSIZE;
    uint64 stack_used = stack_top - current_sp;
    
    printf("%s: sp=0x%lx, stack_used=%ld bytes (%.1f%%)\n", 
           location, current_sp, stack_used, 
           (double)stack_used * 100.0 / PGSIZE);
    
    if (stack_used > PGSIZE * 0.8) {  // 超过80%使用率
        printf("WARNING: Stack usage high!\n");
    }
    
    if (current_sp < stack_bottom || current_sp > stack_top) {
        printf("ERROR: Stack pointer out of bounds!\n");
    }
}

void debug_context_size(void) {
    printf("=== Context Structure Debug ===\n");
    printf("sizeof(context_t) = %ld bytes\n", sizeof(context_t));
    printf("Expected size = %d bytes (14 * 8)\n", 14 * 8);
    
    context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    
    printf("Offset of ra:  %ld (expected 0)\n", (uint64)&ctx.ra - (uint64)&ctx);
    printf("Offset of sp:  %ld (expected 8)\n", (uint64)&ctx.sp - (uint64)&ctx);
    printf("Offset of s0:  %ld (expected 16)\n", (uint64)&ctx.s0 - (uint64)&ctx);
    printf("Offset of s1:  %ld (expected 24)\n", (uint64)&ctx.s1 - (uint64)&ctx);
    printf("Offset of s11: %ld (expected 104)\n", (uint64)&ctx.s11 - (uint64)&ctx);
    printf("=== End Context Debug ===\n");
}

void debug_proc_pointer(const char *location) {
    proc_t *p = myproc();
    
    printf("DEBUG[%s]: myproc()=0x%lx\n", location, (uint64)p);
    
    if (p) {
        printf("  PID=%d, name=%s\n", p->pid, p->name);
        printf("  state=%d\n", p->state);
        printf("  ctx.ra=0x%lx, ctx.sp=0x%lx\n", p->ctx.ra, p->ctx.sp);
        
        // 验证进程指针是否在进程表范围内
        if (p < proc_table || p >= &proc_table[MAX_PROC]) {
            printf("  ⚠️ WARNING: process pointer out of proc_table range!\n");
        }
    } else {
        printf("  NULL process\n");
    }
}
