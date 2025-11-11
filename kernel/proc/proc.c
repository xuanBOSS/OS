// kernel/proc/proc.c
#include "lib/print.h"
#include "mem/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/kvm.h"
#include "proc/cpu.h"
#include "trap/trap.h"
#include "memlayout.h"
#include "riscv.h"
#include "common.h"
#include "fs/file.h"

//#define USE_TEST_PROGRAM      // 普通测试
#define USE_SECURITY_TEST  // 安全测试

#ifdef USE_SECURITY_TEST
    #include "security_test.h"
    #define CURRENT_PROGRAM security_test_bin
    #define CURRENT_PROGRAM_LEN security_test_bin_len
#elif defined(USE_TEST_PROGRAM)
    #include "test.h"
    #define CURRENT_PROGRAM test_bin
    #define CURRENT_PROGRAM_LEN test_bin_len
#else
    #include "initcode.h"
    #define CURRENT_PROGRAM initcode_bin
    #define CURRENT_PROGRAM_LEN initcode_bin_len
#endif

static void init_process_standard_files(proc_t* p);

// 全局进程表（保留但简化）
static proc_t proc_table[MAX_PROC];

// 第一个进程
proc_t proczero;

// ✅ 移除未使用的变量和函数声明
// static spinlock_t proc_table_lock;  // 删除
// static int next_pid = 1;            // 删除
// static proc_t* alloc_proc(void);    // 删除
// static void free_proc(proc_t* p);   // 删除
// static void init_process_files(proc_t* p);  // 删除

static inline int min(int a, int b) {
    return (a < b) ? a : b;
}

// 进程初始化
void proc_init(void) {
    printf("Initializing process management system...\n");
    
    // 初始化进程表
    for (int i = 0; i < MAX_PROC; i++) {
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
        
        // ✅ 初始化文件描述符表
        for (int j = 0; j < NOFILE; j++) {
            proc_table[i].ofile[j] = NULL;
        }
    }
    
    // ✅ 初始化 proczero
    memset(&proczero, 0, sizeof(proc_t));
    proczero.state = PROC_UNUSED;
    proczero.heap_top = USER_HEAP_BASE;
    proczero.ustack_pages = 1;
    
    // 初始化 proczero 的文件描述符表
    for (int i = 0; i < NOFILE; i++) {
        proczero.ofile[i] = NULL;
    }
    
    printf("Process management system initialized\n");
}

// 创建用户页表 - 修复用户页面映射
pgtbl_t proc_pgtbl_init(uint64 trapframe_pa)
{
    printf("Creating user page table...\n");
    
    extern pagetable_t kernel_pagetable;
    
    pgtbl_t pgtbl = create_pagetable();
    if (!pgtbl) {
        printf("Failed to create user page table\n");
        return 0;
    }
    
    printf("Copying kernel page table entries...\n");
    
    // 复制内核页表的所有条目
    uint64* kernel_pte = (uint64*)kernel_pagetable;
    uint64* user_pte = (uint64*)pgtbl;
    
    for (int i = 0; i < 512; i++) {
        if (kernel_pte[i] & PTE_V) {
            user_pte[i] = kernel_pte[i];
        }
    }
    
    printf("Kernel page table copied\n");
    
    // ✅ 关键：映射 trapframe 到正确的虚拟地址
    printf("Mapping trapframe: va=0x%lx -> pa=0x%lx\n", 
           TRAPFRAME, trapframe_pa);
    
    // 强制映射 trapframe，即使可能已存在
    if (map_page(pgtbl, TRAPFRAME, trapframe_pa, PTE_R | PTE_W) != 0) {
        printf("Warning: trapframe mapping failed (may already exist)\n");
    }
    
    printf("User page table created successfully\n");
    return pgtbl;
}

// 创建第一个用户进程
void proc_make_first(){
    printf("=== Creating First User Process ===\n");
    
    proc_t* p = &proczero;
    
    // === 1. 基本设置 ===
    p->pid = 1;
    p->state = PROC_EMBRYO;
    p->privilege_level = 0; 
    
    memset(p->name, 0, sizeof(p->name));
    strcpy(p->name, "init");
    printf("Process name set to: %s\n", p->name);
    
    // === 2. 分配 trapframe ===
    p->tf = (struct trapframe*)pmem_alloc(false);
    if (!p->tf) panic("failed to allocate trapframe");
    memset(p->tf, 0, sizeof(struct trapframe));
    printf("Trapframe allocated at: 0x%lx\n", (uint64)p->tf);
    
    // === 3. 分配内核栈 ===
    uint64 kstack_pa = (uint64)pmem_alloc(true);
    if (!kstack_pa) panic("failed to allocate kernel stack");
    p->kstack = kstack_pa;
    printf("Kernel stack allocated: 0x%lx\n", kstack_pa);
    
    // === 4. 创建用户页表 ===
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    if (!p->pgtbl) panic("failed to create page table");
    printf("User page table created: 0x%lx\n", (uint64)p->pgtbl);
    
    // === 5. 映射内核栈 ===
    extern pagetable_t kernel_pagetable;
    uint64 kstack_va = KSTACK(0);
    printf("Mapping kernel stack in kernel page table:\n");
    printf("  VA: 0x%lx -> PA: 0x%lx\n", kstack_va, kstack_pa);
    
    if (map_page(kernel_pagetable, kstack_va, kstack_pa, PTE_R | PTE_W) != 0) {
        panic("failed to map kernel stack");
    }
    printf("Kernel stack mapped: VA=0x%lx -> PA=0x%lx (RW only)\n", kstack_va, kstack_pa);
    
// === 6. 加载用户程序 ===
uint64 code_va = 0x1000;

// ✅ 修改：使用新的宏定义
uint32 program_size = CURRENT_PROGRAM_LEN;
printf("Program size: %d bytes, pages needed: ", program_size);

uint32 pages_needed = (program_size + PGSIZE - 1) / PGSIZE;  // 向上取整
printf("%d\n", pages_needed);

// ✅ 分配多个页面
for (uint32 i = 0; i < pages_needed; i++) {
    uint64 code_pa = (uint64)pmem_alloc(false);
    if (!code_pa) panic("failed to allocate code page");
    
    printf("Loading program page %d to PA: 0x%lx\n", i, code_pa);
    memset((void*)code_pa, 0, PGSIZE);  // ✅ 确保整页都清零
    
    // 复制程序数据到这个页面
    uint32 copy_size = PGSIZE;
    uint32 offset = i * PGSIZE;
    
    if (offset < program_size) {
        if (offset + PGSIZE > program_size) {
            copy_size = program_size - offset;
        }
        
        // ✅ 修改：使用新的宏定义
        memcpy((void*)code_pa, CURRENT_PROGRAM + offset, copy_size);
        printf("Copied %d bytes to page %d (offset %d)\n", copy_size, i, offset);
    }
    
    // ✅ 对于第2页，确保剩余部分可以用作数据段
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

// ✅ 设置堆起始地址在代码页之后
p->heap_top = code_va + pages_needed * PGSIZE;
printf("Heap initialized at: 0x%lx\n", p->heap_top);
printf("Total program loaded: %d bytes in %d pages\n", program_size, pages_needed);

// === 7. 用户栈映射 ===
// ✅ 修改：栈地址要避开堆空间
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
p->tf->kernel_sp = KSTACK(0) + PGSIZE;
p->tf->kernel_trap = (uint64)trap_user_handler;

p->tf->epc = 0x1000;
p->tf->sp = ustack_va + PGSIZE - 8;  // ✅ 使用新的栈地址

// ✅ 设置正确的堆起始地址
p->heap_top = heap_start;

printf("Trapframe configured:\n");
printf("  epc: 0x%lx (user program entry)\n", p->tf->epc);
printf("  sp: 0x%lx (user stack top)\n", p->tf->sp);
printf("  heap_top: 0x%lx\n", p->heap_top);

    // ✅ 清零所有用户寄存器
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

    printf("Trapframe configured:\n");
    printf("  epc: 0x%lx (user program entry)\n", p->tf->epc);
    printf("  sp: 0x%lx (user stack top)\n", p->tf->sp);

    // === 9. 初始化文件描述符 ===
    printf("Initializing standard file descriptors...\n");
    init_process_standard_files(p);

    // === 10. 切换到用户模式 ===
    p->state = PROC_RUNNABLE;
    cpu_t* cpu = mycpu();
    cpu->proc = p;

    printf("About to switch to user program at 0x%lx\n", p->tf->epc);
    
    // ✅ 添加更多调试信息
    printf("Debug: User program details:\n");
    printf("  EPC: 0x%lx\n", p->tf->epc);
    printf("  SP: 0x%lx\n", p->tf->sp);
    printf("  SATP: 0x%lx\n", p->tf->kernel_satp);
    printf("  Page table: 0x%lx\n", (uint64)p->pgtbl);
    
    printf("Debug: Starting user process switch...\n");
    
    // 切换到调度器
    trap_user_return();
    
    // ✅ 如果到这里说明 scheduler 返回了（不应该发生）
    printf("ERROR: scheduler() returned!\n");
    panic("scheduler returned");
}

void init_process_standard_files(proc_t* p)
{
    printf("Initializing standard file descriptors...\n");
    
    // 清空文件描述符表
    for (int i = 0; i < NOFILE; i++) {
        p->ofile[i] = NULL;
    }
    
    // ✅ 修复：正确初始化标准文件描述符
    for (int fd = 0; fd < 3; fd++) {
        struct file *f = filealloc();
        if (!f) {
            printf("ERROR: Failed to allocate file for fd %d\n", fd);
            panic("Cannot initialize standard files");
        }
        
        // ✅ 关键修复：确保所有字段都正确设置
        f->type = FD_DEVICE_E;
        f->major = CONSOLE;
        f->readable = (fd == 0) ? 1 : 0;  // stdin 可读
        f->writable = (fd == 0) ? 0 : 1;  // stdout/stderr 可写
        f->ref = 1;
        f->off = 0;  // ✅ 添加：设置文件偏移
        
        // ✅ 关键：确保设备文件有正确的操作函数
        // 这通常在设备初始化时设置，但我们需要确保它存在
        
        p->ofile[fd] = f;
        printf("✅ Initialized fd %d: type=%d, major=%d, readable=%d, writable=%d, ref=%d\n", 
               fd, f->type, f->major, f->readable, f->writable, f->ref);
    }
    
    printf("Standard file descriptors initialized successfully\n");
}
