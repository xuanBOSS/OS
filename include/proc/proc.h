#ifndef __PROC_H__
#define __PROC_H__

#include "common.h"
#include "memlayout.h"
#include "lib/lock.h"

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
    spinlock_t lock;                // 进程锁
    
    // 基本信息
    int pid;                        // 进程ID
    proc_state_t state;             // 进程状态
    char name[16];                  // 进程名
    int privilege_level;            // 特权级别
    
    // 内存管理
    pgtbl_t pgtbl;                 // 用户页表 (修改：使用 pgtbl_t)
    uint64 kstack;                 // 内核栈
    trapframe_t *tf;               // 陷阱帧 (修改：使用具体类型)
    uint64 heap_top;               // 堆顶指针
    int ustack_pages;              // 用户栈页数

    context_t ctx;                 // 内核上下文（用于进程切换）
    void* wait_chan;               // 等待通道
    int killed;                    // 是否被杀死
    struct proc* next;             // 链表指针
    
    // 进程关系
    struct proc* parent;           // 父进程
    struct proc* children;         // 子进程链表
    struct proc* sibling;          // 兄弟进程链表
    int exit_code;                 // 退出码
    
    // 文件系统
    struct file* ofile[NOFILE];    // 打开的文件表
    struct inode* cwd;             // 当前工作目录
    
    // 信号处理（简化版）
    uint64 pending_signals;        // 待处理信号
    uint64 signal_mask;            // 信号掩码
    
} proc_t;

#define PRIVILEGE_USER      0   // 用户级别
#define PRIVILEGE_SYSTEM    1   // 系统级别
#define PRIVILEGE_KERNEL    2   // 内核级别

// 进程管理全局变量和函数
#define MAX_PROC 64

// 全局变量声明
extern proc_t proczero;            // 第一个进程

// 函数声明
void     proc_init(void);                         // 初始化进程管理系统
void     proc_make_first();                        // 创建第一个进程并切换到它执行
pgtbl_t  proc_pgtbl_init(uint64 trapframe);       // 进程页表的初始化和基本映射
proc_t*  myproc(void);                            // 获取当前进程

#endif
