// kernel/proc/proc.c
#include "lib/print.h"
#include "mem/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/kvm.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "trap/trap.h"
#include "memlayout.h"
#include "riscv.h"
#include "common.h"

// 全局变量
proc_t proc_table[MAX_PROC];

// 第一个进程
static proc_t proczero;

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
    }
    
    printf("Process management system initialized\n");
}

// 创建用户页表
pgtbl_t proc_pgtbl_init(uint64 trapframe_pa)
{
    printf("Creating user page table...\n");
    
    // 创建用户页表
    pgtbl_t pgtbl = create_pagetable();
    if (!pgtbl) {
        printf("Failed to create user page table\n");
        return 0;
    }
    
    // 🔥 关键修复：映射 trampoline 页面
    extern char trampoline[];
    printf("Mapping trampoline: va=0x%lx -> pa=0x%lx\n", 
           TRAMPOLINE, (uint64)trampoline);
    
    if (map_page(pgtbl, TRAMPOLINE, (uint64)trampoline, PTE_R | PTE_X) != 0) {
        printf("Failed to map trampoline\n");
        destroy_pagetable(pgtbl);
        return 0;
    }
    
    // 🔥 关键修复：映射 trapframe 页面  
    printf("Mapping trapframe: va=0x%lx -> pa=0x%lx\n", 
           TRAPFRAME, trapframe_pa);
    
    if (map_page(pgtbl, TRAPFRAME, trapframe_pa, PTE_R | PTE_W) != 0) {
        printf("Failed to map trapframe\n");
        destroy_pagetable(pgtbl);
        return 0;
    }
    
    printf("User page table created successfully\n");
    return pgtbl;
}

// 创建第一个用户进程
void proc_make_first()
{
    printf("=== Creating First User Process ===\n");
    
    proc_t* p = &proczero;
    
    // === 1. 基本设置 ===
    p->pid = 1;
    p->state = PROC_EMBRYO;
    
    // ✅ 修复：手动设置进程名，避免 strcpy
    memset(p->name, 0, sizeof(p->name));
    p->name[0] = 'i';
    p->name[1] = 'n';
    p->name[2] = 'i';
    p->name[3] = 't';
    p->name[4] = '\0';
    
    printf("Process name set to: %s\n", p->name);
    
    // === 2. 分配 trapframe ===
    p->tf = (trapframe_t*)pmem_alloc(false);
    if (!p->tf) panic("failed to allocate trapframe");
    memset(p->tf, 0, sizeof(trapframe_t));
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
    
    // ✅ 修复：内核栈只需要读写权限
    if (map_page(kernel_pagetable, kstack_va, kstack_pa, PTE_R | PTE_W) != 0) {
        panic("failed to map kernel stack");
    }
    printf("Kernel stack mapped: VA=0x%lx -> PA=0x%lx (RW only)\n", kstack_va, kstack_pa);
    
    // === 6. 用户代码页映射 ===
    uint64 code_va = USER_TEXT_BASE;  // 0x1000
    uint64 code_pa = (uint64)pmem_alloc(false);
    if (!code_pa) panic("failed to allocate code page");
    
    printf("Writing user program to PA: 0x%lx\n", code_pa);
    unsigned int* code_ptr = (unsigned int*)code_pa;
    
    // ✅ 完整的测试程序
    code_ptr[0] = 0x00000893;  // li a7, 0     (syscall 0)
    code_ptr[1] = 0x00000073;  // ecall
    code_ptr[2] = 0x00100893;  // li a7, 1     (syscall 1)  
    code_ptr[3] = 0x00000073;  // ecall
    code_ptr[4] = 0x00300893;  // li a7, 3     (syscall 3 - exit)
    code_ptr[5] = 0x00000073;  // ecall
    code_ptr[6] = 0xfe9ff06f;  // j -24        (jump back to start)
    
    printf("Complete user program:\n");
    printf("  [0] 0x%x (li a7, 0)\n", code_ptr[0]);
    printf("  [1] 0x%x (ecall)\n", code_ptr[1]);
    printf("  [2] 0x%x (li a7, 1)\n", code_ptr[2]);
    printf("  [3] 0x%x (ecall)\n", code_ptr[3]);
    printf("  [4] 0x%x (li a7, 3)\n", code_ptr[4]);
    printf("  [5] 0x%x (ecall)\n", code_ptr[5]);
    printf("  [6] 0x%x (j -24)\n", code_ptr[6]);
    
    // ✅ 关键修复：在用户页表中映射
    if (map_page(p->pgtbl, code_va, code_pa, PTE_R | PTE_X | PTE_U) != 0) {
        panic("failed to map code page in user page table");
    }
    printf("Code mapped in user page table: VA=0x%lx -> PA=0x%lx\n", code_va, code_pa);
    
    // ✅ 关键修复：在内核页表中也映射用户代码（添加 PTE_U 标志）
    if (map_page(kernel_pagetable, code_va, code_pa, PTE_R | PTE_X | PTE_U) != 0) {
        panic("failed to map code page in kernel page table");
    }
    printf("Code ALSO mapped in kernel page table: VA=0x%lx -> PA=0x%lx (with PTE_U)\n", code_va, code_pa);
    
    // === 7. 用户栈映射 ===
    uint64 ustack_va = USER_STACK_TOP - PGSIZE;  // 0x3000
    uint64 ustack_pa = (uint64)pmem_alloc(false);
    if (!ustack_pa) panic("failed to allocate user stack");
    
    if (map_page(p->pgtbl, ustack_va, ustack_pa, PTE_R | PTE_W | PTE_U) != 0) {
        panic("failed to map user stack");
    }
    printf("Stack mapped: VA=0x%lx -> PA=0x%lx\n", ustack_va, ustack_pa);
    
    // === 8. 设置 trapframe ===
    p->tf->kernel_satp = MAKE_SATP(kernel_pagetable);
    p->tf->kernel_sp = kstack_va + PGSIZE;
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = mycpuid();
    
    // ✅ 关键：确保用户程序从正确地址开始
    p->tf->epc = code_va;           // 0x1000 - 用户程序入口
    p->tf->sp = USER_STACK_TOP;     // 0x4000 - 用户栈顶
    
    printf("Trapframe configured:\n");
    printf("  kernel_satp: 0x%lx\n", p->tf->kernel_satp);
    printf("  kernel_sp: 0x%lx (should be 0x%lx)\n", p->tf->kernel_sp, kstack_va + PGSIZE);
    printf("  kernel_trap: 0x%lx\n", p->tf->kernel_trap);
    printf("  epc: 0x%lx\n", p->tf->epc);
    printf("  sp: 0x%lx\n", p->tf->sp);

    printf("Process configured: PID=%d, epc=0x%lx, sp=0x%lx\n", 
           p->pid, p->tf->epc, p->tf->sp);

    // === 9. 设置进程并切换 ===
    cpu_t* cpu = mycpu();
    cpu->proc = p;
    p->state = PROC_RUNNING;
    
    // ✅ 关键修复：使用实际的 trapframe 物理地址
    uint64 trapframe_pa = (uint64)p->tf;
    w_sscratch(trapframe_pa);
    
    printf("Switching to user mode...\n");
    trap_user_return();
    
    panic("should not return");
}
