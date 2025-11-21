#ifndef __PROC_H__
#define __PROC_H__

#include "common.h"
#include "memlayout.h"
#include "lib/lock.h"
#include "mem/vmem.h" 

// 前向声明，避免循环依赖
struct file;
struct inode;

// 确保 NOFILE 定义
#ifndef NOFILE
#define NOFILE 16  // 每个进程最大文件描述符数
#endif

// 页表类型定义
typedef uint64* pgtbl_t;
// 为了兼容性，也定义 pagetable_t
typedef pgtbl_t pagetable_t;

// 进程状态枚举
typedef enum {
    PROC_UNUSED = 0,    // 未使用
    PROC_EMBRYO,        // 正在创建
    PROC_RUNNABLE,      // 可运行
    PROC_RUNNING,       // 正在运行
    PROC_SLEEPING,      // 睡眠等待
    PROC_ZOMBIE         // 僵尸状态
} proc_state_t;

// context 定义 - 用于内核态进程切换
typedef struct context {
    uint64 ra; // 返回地址
    uint64 sp; // 栈指针

    // callee-saved 寄存器
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
} context_t;

// trapframe 定义 - 用于用户态/内核态切换
typedef struct trapframe {
    /*   0 */ uint64 kernel_satp;   // kernel page table
    /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
    /*  16 */ uint64 kernel_trap;   // usertrap()
    /*  24 */ uint64 epc;           // saved user program counter
    /*  32 */ uint64 kernel_hartid; // saved kernel tp
    
    // 用户寄存器（与 trampoline.S 中的偏移对应）
    /*  40 */ uint64 ra;     // x1
    /*  48 */ uint64 sp;     // x2
    /*  56 */ uint64 gp;     // x3
    /*  64 */ uint64 tp;     // x4
    /*  72 */ uint64 t0;     // x5
    /*  80 */ uint64 t1;     // x6
    /*  88 */ uint64 t2;     // x7
    /*  96 */ uint64 s0;     // x8
    /* 104 */ uint64 s1;     // x9
    /* 112 */ uint64 a0;     // x10 - 这里需要保存原始的用户 a0
    /* 120 */ uint64 a1;     // x11
    /* 128 */ uint64 a2;     // x12
    /* 136 */ uint64 a3;     // x13
    /* 144 */ uint64 a4;     // x14
    /* 152 */ uint64 a5;     // x15
    /* 160 */ uint64 a6;     // x16
    /* 168 */ uint64 a7;     // x17
    /* 176 */ uint64 s2;     // x18
    /* 184 */ uint64 s3;     // x19
    /* 192 */ uint64 s4;     // x20
    /* 200 */ uint64 s5;     // x21
    /* 208 */ uint64 s6;     // x22
    /* 216 */ uint64 s7;     // x23
    /* 224 */ uint64 s8;     // x24
    /* 232 */ uint64 s9;     // x25
    /* 240 */ uint64 s10;    // x26
    /* 248 */ uint64 s11;    // x27
    /* 256 */ uint64 t3;     // x28
    /* 264 */ uint64 t4;     // x29
    /* 272 */ uint64 t5;     // x30
    /* 280 */ uint64 t6;     // x31
} trapframe_t;

// 进程结构
typedef struct proc {
    // ========== 第一部分：基本信息 ==========
    spinlock_t lock;
    int pid;
    proc_state_t state;
    int privilege_level;
    char name[16];
    
    // ========== 第二部分：内存管理 ==========
    pgtbl_t pgtbl;
    uint64 kstack;
    trapframe_t *tf;
    uint64 heap_top;
    int ustack_pages;
    
    // ========== 第三部分：进程关系 ==========
    struct proc* parent;
    struct proc* children;
    struct proc* sibling;
    int exit_code;
    
    // ========== 第四部分：同步相关 ==========
    void* wait_chan;
    int killed;
    struct proc* next;
    
    // ========== 第五部分：文件系统 ==========
    struct file* ofile[NOFILE];
    struct inode* cwd;
    
    // ========== 第六部分：信号处理 ==========
    uint64 pending_signals;
    uint64 signal_mask;
    
    // ========== 第七部分：上下文（放在最后！） ==========
    context_t ctx;  // ✅ 移到最后，避免被覆盖
    
} proc_t;

#define PRIVILEGE_USER      0   // 用户级别
#define PRIVILEGE_SYSTEM    1   // 系统级别
#define PRIVILEGE_KERNEL    2   // 内核级别

// 进程管理全局变量和函数
#define MAX_PROC 64

// 全局变量声明
extern proc_t proczero;            // 第一个进程
extern proc_t proc_table[MAX_PROC];     // 进程表
extern proc_t *initproc;                // init进程指针
extern struct spinlock wait_lock;       // wait系统调用锁

// 函数声明
void     proc_init(void);                         // 初始化进程管理系统
void     proc_make_first();                        // 创建第一个进程并切换到它执行
pgtbl_t  proc_pgtbl_init(uint64 trapframe);       // 进程页表的初始化和基本映射
proc_t*  myproc(void);       
proc_t*  proc_alloc(void);                        // 分配新进程
void     proc_free(proc_t* p);                    // 释放进程资源                     // 获取当前进程

int proc_copy_memory(proc_t *parent, proc_t *child);
void proc_copy_files(proc_t *parent, proc_t *child);
void proc_free_memory(proc_t *p);
proc_t* find_child(proc_t *parent, int pid);
void wakeup(void *chan);
void sleep(void *chan, struct spinlock *lk);
void forkret(void);
void sched(void);

char* safestrcpy(char *s, const char *t, int n);
int argint(int n, int *ip);
int argaddr(int n, uint64 *ip);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);
void proc_freepagetable(pagetable_t pagetable, uint64 sz);  
void swtch(context_t *old, context_t *new);                 
void scheduler(void);                                       

void validate_stack_pointer(proc_t *p);
void check_stack_usage(const char* location);
void debug_context_size(void);
void debug_proc_pointer(const char *location);

#endif
