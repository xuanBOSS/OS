#ifndef __PROC_H__
#define __PROC_H__

#include "common.h"
#include "memlayout.h"

// 页表类型定义
typedef uint64* pgtbl_t;

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

// 进程定义
typedef struct proc {
    int pid;                 // 进程标识符
    proc_state_t state;      // 进程状态

    // 内存管理
    pgtbl_t pgtbl;           // 用户态页表
    uint64 heap_top;         // 用户堆顶(以字节为单位)
    uint64 ustack_pages;     // 用户栈占用的页面数量
    trapframe_t* tf;         // 用户态内核态切换时的运行环境暂存空间

    // 调度相关
    uint64 kstack;           // 内核栈的虚拟地址
    context_t ctx;           // 内核态进程上下文

    // 同步与状态
    void* wait_chan;         // 等待通道
    int exit_code;           // 退出状态码
    int killed;              // 被杀死标志
    
    // 进程关系
    struct proc* parent;     // 父进程指针
    
    // 链表管理
    struct proc* next;       // 用于状态链表

    // 添加进程名字段
    char name[16];           // 进程名

} proc_t;

// 进程管理全局变量和函数
#define MAX_PROC 64

// 函数声明
void     proc_init(void);                         // 初始化进程管理系统
void     proc_make_first();                        // 创建第一个进程并切换到它执行
pgtbl_t  proc_pgtbl_init(uint64 trapframe);       // 进程页表的初始化和基本映射
proc_t*  myproc(void);                            // 获取当前进程

#endif
