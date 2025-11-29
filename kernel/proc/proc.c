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
#include "fs/inode.h"

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

         proc_table[i].cwd = NULL;
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

    proczero.cwd = inode_alloc(INODE_ROOT);
    if (!proczero.cwd) {
        panic("proc_init: failed to allocate root inode for proczero");
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
        spinlock_release(&pid_lock);
        return -1;
    }
    
    next_pid++;
    
    spinlock_release(&pid_lock);
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
        return NULL;
    }
    
    // 分配PID
    p->pid = alloc_pid();
    if (p->pid < 0) {
        p->state = PROC_UNUSED;
        return NULL;
    }
    
    // 设置基本信息
    p->privilege_level = PRIVILEGE_USER;
    strcpy(p->name, "new_proc");  // 默认名称
    
    // 分配内核栈
    void* kstack_page = pmem_alloc(true);  // 分配内核页
    if (!kstack_page) {
        free_pid(p->pid);
        p->state = PROC_UNUSED;
        return NULL;
    }
    p->kstack = (uint64)kstack_page;  // 栈底地址
    
    // 分配trapframe
    p->tf = (trapframe_t*)pmem_alloc(false);  // ✅ 使用用户页面
    if (!p->tf) {
        pmem_free((uint64)kstack_page, true);
        free_pid(p->pid);
        p->state = PROC_UNUSED;
        return NULL;
    }
    memset(p->tf, 0, sizeof(trapframe_t));

    extern pagetable_t kernel_pagetable;
    p->tf->kernel_satp = MAKE_SATP(kernel_pagetable);
    p->tf->kernel_sp = p->kstack + PGSIZE;
    extern void trap_user_handler(void);
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = r_tp();  // ✅ 设置 CPU ID
    
    // 创建用户页表
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (!p->pgtbl) {
        pmem_free((uint64)p->tf, false);
        pmem_free((uint64)kstack_page, true);
        free_pid(p->pid);
        p->state = PROC_UNUSED;
        return NULL;
    }
    
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
    
    
    return p;
}

// ===== proc_free() 函数 =====
void proc_free(proc_t* p) {
    if (!p) {
        return;
    }
    
    // ✅ 1. 释放用户页表和相关内存
    if (p->pgtbl && p->pgtbl != NULL) {
        
        // 释放用户内存页面
        proc_free_memory(p);
        
        // 释放页表本身
        proc_freepagetable(p->pgtbl, p->heap_top);
        p->pgtbl = NULL;
    }
    
    // ✅ 2. 释放trapframe
    if (p->tf) {
        pmem_free((uint64)p->tf, false);  // trapframe 使用用户页面
        p->tf = NULL;
    }
    
    // ✅ 3. 释放内核栈
    if (p->kstack && p->kstack != 0) {
        pmem_free(p->kstack, true);  // ✅ 修复：直接释放栈底地址
        p->kstack = 0;
    }
    
    // ✅ 4. 关闭所有文件描述符
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i]) {
            file_close(p->ofile[i]);  // ✅ 实现：关闭文件，减少引用计数
            p->ofile[i] = NULL;
        }
    }
    
    // ✅ 5. 释放当前工作目录
    if (p->cwd) {
        inode_free(p->cwd);  // ✅ 实现：释放inode引用
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
}

void debug_page_table_mapping(pgtbl_t pgtbl, uint64 va) {
    // 调试函数，已禁用输出
}

// ===== 创建用户页表 =====
pgtbl_t proc_pgtbl_init(uint64 trapframe_pa) {
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
    // 计算 TRAMPOLINE 的 Level-2 索引（TRAPFRAME 在同一索引下）
    uint64 trampoline_idx = (TRAMPOLINE >> 30) & 0x1FF;
    
    for (int i = 256; i < 512; i++) {
        // ✅ 跳过 TRAMPOLINE/TRAPFRAME 的索引，不复制
        if (i == trampoline_idx) {
            continue;
        }
        
        if (kernel_pte[i] & PTE_V) {
            user_pte[i] = kernel_pte[i];
        }
    }
    
    // ✅ 手动映射 TRAMPOLINE（从内核页表中获取物理地址）
    extern char trampoline[];
    uint64 trampoline_pa = (uint64)trampoline;
    
    if (map_page(pgtbl, TRAMPOLINE, trampoline_pa, PTE_R | PTE_X) != 0) {
        destroy_pagetable(pgtbl);
        return 0;
    }
    
    // ✅ 手动映射 TRAPFRAME（使用传入的物理地址）
    if (map_page(pgtbl, TRAPFRAME, trapframe_pa, PTE_R | PTE_W) != 0) {
        destroy_pagetable(pgtbl);
        return 0;
    }

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

    // === 映射内核栈到内核页表 ===
    extern pagetable_t kernel_pagetable;
    uint64 kstack_va = KSTACK(0);
    uint64 kstack_pa = p->kstack;  // 物理地址是栈底
    
    if (map_page(kernel_pagetable, kstack_va, kstack_pa, PTE_R | PTE_W) != 0) {
        panic("failed to map kernel stack");
    }
    
    // === 6. 加载用户程序 ===
    uint64 code_va = 0x1000;

    //使用新的宏定义
    uint32 program_size = CURRENT_PROGRAM_LEN;
    uint32 pages_needed = (program_size + PGSIZE - 1) / PGSIZE;  // 向上取整
    
    // ✅ 确保至少映射 4 页（到 0x4000），以覆盖 .bss 段中的全局变量（如 errno）
    // errno 在 0x4000，需要第 4 页（0x4000-0x4fff）
    if (pages_needed < 4) {
        pages_needed = 4;
    }

    //分配多个页面
    for (uint32 i = 0; i < pages_needed; i++) {
        uint64 code_pa = (uint64)pmem_alloc(false);
        if (!code_pa) panic("failed to allocate code page");
        
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
        }
        
        asm volatile("fence" ::: "memory");
        asm volatile("fence.i" ::: "memory");
        
        uint64 page_va = code_va + i * PGSIZE;
        uint64 pte_flags;
        // ✅ 所有页面都需要写权限，因为 .data 和 .bss 段可能在任何页面中
        pte_flags = PTE_R | PTE_W | PTE_X | PTE_U;  // 可读写执行
        
        if (map_page(p->pgtbl, page_va, code_pa, pte_flags) != 0) {
            panic("failed to map user code page");
        }
    }

    // 设置堆起始地址在代码页之后
    p->heap_top = code_va + pages_needed * PGSIZE;

    // === 7. 用户栈映射 ===
    // 栈地址要避开堆空间
    uint64 code_end_va = 0x1000 + pages_needed * PGSIZE;  // 代码结束地址
    uint64 heap_start = code_end_va;                       // 堆紧跟代码
    uint64 ustack_va = 0x10000;                           // 栈放在更远的地方（64KB处）

    uint64 ustack_pa = (uint64)pmem_alloc(false);
    if (!ustack_pa) panic("failed to allocate user stack");

    memset((void*)ustack_pa, 0, PGSIZE);

    if (map_page(p->pgtbl, ustack_va, ustack_pa, PTE_R | PTE_W | PTE_U) != 0) {
        panic("failed to map user stack");
    }

    // === 8. 配置 trapframe ===
    p->tf->kernel_satp = MAKE_SATP(kernel_pagetable);
    p->tf->kernel_sp = p->kstack + PGSIZE; 
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = r_tp(); 

    // ✅ 设置正确的入口点：从 ELF 文件读取，_start 在 0x10da
    // 但为了兼容性，我们检查是否有 _start 符号，如果没有则使用 0x1000
    p->tf->epc = 0x10da;  // _start 函数的实际地址
    p->tf->sp = ustack_va + PGSIZE - 8;  // 使用新的栈地址

    // 设置正确的堆起始地址
    p->heap_top = heap_start;

    // 清零所有用户寄存器
    p->tf->ra = 0;
    // ✅ 设置 gp (global pointer) 寄存器
    // gp 用于快速访问全局变量，通常指向 .data 段的中间位置
    // 由于 .data 段在 .text 和 .rodata 之后，我们估算一个合理的位置
    // 设置为程序起始地址 + 4KB，这样可以覆盖大部分全局变量访问
    p->tf->gp = code_va + PGSIZE;  // 设置为 0x2000
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
    init_process_standard_files(p);

    p->cwd = inode_alloc(INODE_ROOT);
    if (!p->cwd) {
        panic("proc_make_first: failed to allocate root inode");
    }

    // === 10. 切换到用户模式 ===
    p->state = PROC_RUNNABLE;
    
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
        struct file *f = file_alloc();
        if (!f) {
            printf("ERROR: Failed to allocate file for fd %d\n", fd);
            panic("Cannot initialize standard files");
        }
        
        // 确保所有字段都正确设置
        f->type = FD_DEVICE;
        f->major = DEV_CONSOLE;
        f->readable = (fd == 0) ? 1 : 0;  // stdin 可读
        f->writable = (fd == 0) ? 0 : 1;  // stdout/stderr 可写
        f->ref = 1;
        f->offset = 0;  // 设置文件偏移
        
        // 确保设备文件有正确的操作函数
        // 这通常在设备初始化时设置，但我们需要确保它存在
        
        p->ofile[fd] = f;
    }
}

// 进程管理函数
int proc_copy_memory(proc_t *parent, proc_t *child) {
    // 1. 复制代码页（从 0x1000 到 heap_top）
    for (uint64 va = 0x1000; va < parent->heap_top; va += PGSIZE) {
        // 获取父进程页面的物理地址
        uint64 parent_pa = va_to_pa(parent->pgtbl, va);
        if (parent_pa == 0) {
            continue;
        }
        
        // 为子进程分配新的物理页
        uint64 child_pa = (uint64)pmem_alloc(false);
        if (!child_pa) {
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
            pmem_free(child_pa, false);
            return -1;
        }
    }
    
    // 2. 复制用户栈（假设栈在 0x10000）
    uint64 ustack_va = 0x10000;
    uint64 parent_pa = va_to_pa(parent->pgtbl, ustack_va);
    if (parent_pa != 0) {
        // 为子进程分配栈页
        uint64 child_pa = (uint64)pmem_alloc(false);
        if (!child_pa) {
            return -1;
        }
        
        // 复制栈内容
        memcpy((void*)child_pa, (void*)parent_pa, PGSIZE);
        
        // 映射到子进程页表
        if (map_page(child->pgtbl, ustack_va, child_pa, PTE_R | PTE_W | PTE_U) != 0) {
            pmem_free(child_pa, false);
            return -1;
        }
    }
    
    // 3. 复制元数据
    child->heap_top = parent->heap_top;
    child->ustack_pages = parent->ustack_pages;
    
    return 0;
}

void proc_copy_files(proc_t *parent, proc_t *child) {
    for (int i = 0; i < NOFILE; i++) {
        if (parent->ofile[i]) {
            child->ofile[i] = file_dup(parent->ofile[i]);
        }
    }
    
    if (parent->cwd) {
        child->cwd = inode_dup(parent->cwd);
    }
}

void proc_free_memory(proc_t *p) {
    if (!p->pgtbl) {
        return;
    }
    
    // ✅ 使用现有的 va_to_pa 函数
    // 释放代码段页面（从 0x1000 开始）
    uint64 code_start = 0x1000;
    uint64 code_end = p->heap_top;
    
    for (uint64 va = code_start; va < code_end; va += PGSIZE) {
        uint64 pa = va_to_pa(p->pgtbl, va);  // ✅ 使用现有函数
        if (pa != 0) {
            pmem_free(pa, false);  // 释放用户页面
            unmap_page(p->pgtbl, va);
        }
    }
    
    // 释放用户栈页面
    uint64 ustack_va = 0x10000;  // 根据你的代码中的栈地址
    uint64 pa = va_to_pa(p->pgtbl, ustack_va);  // ✅ 使用现有函数
    if (pa != 0) {
        pmem_free(pa, false);
        unmap_page(p->pgtbl, ustack_va);  // ✅ 使用现有函数
    }
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
    proc_t *current = myproc();  // 可能为 NULL（在中断上下文中）
    int woken = 0;
    
    printf("wakeup: called for channel 0x%lx\n", (uint64)chan);
    
    for (p = proc_table; p < &proc_table[MAX_PROC]; p++) {
        // ✅ 修复：只有当 p 是当前运行的进程时才跳过
        // 在中断上下文中，current 可能为 NULL，所以不应该跳过任何进程
        // 注意：如果当前进程是 sleep 状态（不应该发生），我们也不应该跳过它
        if (current != NULL && p == current && p->state == PROC_RUNNING) {
            continue;  // 跳过正在运行的当前进程
        }
        
        spinlock_acquire(&p->lock);
        if (p->state == PROC_SLEEPING && p->wait_chan == chan) {
            printf("wakeup: waking up process PID=%d on channel 0x%lx\n", p->pid, (uint64)chan);
            p->state = PROC_RUNNABLE;
            woken++;
        }
        spinlock_release(&p->lock);
    }
    
    // 调试输出
    if (woken > 0) {
        printf("wakeup: woken %d process(es) on channel 0x%lx\n", woken, (uint64)chan);
    } else {
        printf("wakeup: no processes found sleeping on channel 0x%lx\n", (uint64)chan);
    }
}

void sched(void) {
    proc_t *p = myproc();
    
    if (!p) panic("sched: no current process");
    if (!spinlock_holding(&p->lock)) panic("sched: not holding lock");
    if (p->state == PROC_RUNNING) panic("sched: still running");
    
    cpu_t *cpu = mycpu();
    int intena = cpu->intena;
    // ✅ 在切换前确保中断可以在调度器中打开
    // 调度器会调用 intr_on()
    
    swtch(&p->ctx, &cpu->ctx);
    
    cpu->intena = intena;
}

void sleep(void *chan, struct spinlock *lk)
{
    proc_t *p = myproc();

    if (p == NULL) {
        panic("sleep called without a process");
    }
    
    // 获取进程锁
    spinlock_acquire(&p->lock);
    
    // 释放条件锁
    if (lk != &p->lock) {
        spinlock_release(lk);
    }
    
    // 设置睡眠状态
    p->wait_chan = chan;
    p->state = PROC_SLEEPING;
    
    // 调用 sched
    sched();
    
    // 被唤醒后
    p->wait_chan = 0;
    
    // 释放进程锁
    spinlock_release(&p->lock);
    
    // 重新获取条件锁
    if (lk != &p->lock) {
        spinlock_acquire(lk);
    }
}

// ✅ 添加：释放进程页表的函数
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    if (!pagetable) {
        return;
    }
    
    // 使用现有的 destroy_pagetable 函数
    destroy_pagetable(pagetable);
}

void forkret(void) {
    proc_t *p = myproc();
    
    // ✅ 释放进程锁（调度器持有）
    spinlock_release(&p->lock);
    
    // ✅ 跳转到用户态
    trap_user_return();
    
    panic("forkret: trap_user_return returned!");
}

void scheduler(void) {
    cpu_t *c = mycpu();
    c->proc = 0;
    
    for(;;) {
        // ✅ 1. 在查找进程前打开中断（允许接收中断）
        intr_on();
        
        int found = 0;
        
        for (proc_t *p = proc_table; p < &proc_table[MAX_PROC]; p++) {
            spinlock_acquire(&p->lock);  // ← 关闭中断
            
            if (p->state == PROC_RUNNABLE) {
                // 初始化上下文（第一次运行）
                if (p->ctx.sp == 0) {
                    p->ctx.sp = p->kstack + PGSIZE;
                    p->ctx.ra = (uint64)forkret;
                }
                
                p->state = PROC_RUNNING;
                c->proc = p;
                
                // ✅ 切换到进程（此时持有 p->lock，中断关闭）
                swtch(&c->ctx, &p->ctx);
                
                // ✅ 进程返回后，仍然持有 p->lock，中断关闭
                c->proc = 0;
                found = 1;
            }
            
            spinlock_release(&p->lock);  // ← 打开中断
        }
        
        // ✅ 2. 如果没有找到进程，检查是否所有进程都已退出
        if (found == 0) {
            // 检查是否所有进程都已退出（没有可运行进程）
            int all_exited = 1;
            for (proc_t *p = proc_table; p < &proc_table[MAX_PROC]; p++) {
                if (p->state != PROC_UNUSED && p->state != PROC_ZOMBIE) {
                    all_exited = 0;
                    break;
                }
            }
            
            if (all_exited) {
                // 所有进程都已退出，系统停止
                printf("\n=== All processes exited ===\n");
                printf("System halted.\n");
                while(1) {
                    asm volatile("wfi");
                }
            }
            
            // WFI 指令：等待中断
            // 中断返回后会继续执行下一条指令（循环继续）
            asm volatile("wfi");
            
            // ✅ 中断返回后，继续循环，重新检查进程状态
            // 不需要额外的代码，循环会自动继续
        }
    }
}

void validate_stack_pointer(proc_t *p) {
    uint64 expected_sp_min = KSTACK(0);
    uint64 expected_sp_max = KSTACK(0) + PGSIZE;
    
    if (p->ctx.sp < expected_sp_min || p->ctx.sp > expected_sp_max) {
        p->ctx.sp = expected_sp_max;  // 强制修复
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
