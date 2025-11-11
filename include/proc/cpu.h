#ifndef __CPU_H__
#define __CPU_H__

#include "common.h"
#include "proc/proc.h"

// 前向声明，避免循环依赖
struct proc;
typedef struct proc proc_t;
struct context;
typedef struct context context_t;

// 声明全局变量（保留你原有的多CPU启动管理）
extern volatile int boot_cpu_id;
extern volatile int cpu_started[NCPU];
extern volatile int init_phase;
extern volatile int secondary_cpus_ready;

// CPU结构（融合老师的proc字段）
typedef struct cpu {
    int noff;       // 关中断的深度
    int origin;     // 第一次关中断前的状态
    proc_t* proc;   // cpu上运行的进程 (老师的版本)
    context_t ctx;  // 内核上下文暂存 (老师的版本)
} cpu_t;

// 全局变量声明
extern cpu_t cpus[NCPU];          // CPU 数组

// 核心接口
int     mycpuid(void);
cpu_t*  mycpu(void);
proc_t* myproc(void);  // 老师的版本

void    cpu_init(void);                    // 初始化CPU系统
void    set_current_proc(proc_t* proc);    // 设置当前进程
void    cpu_stats(void);                   // 显示CPU统计信息

#endif
