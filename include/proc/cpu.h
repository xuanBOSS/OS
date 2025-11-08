#ifndef __CPU_H__
#define __CPU_H__

#include "common.h"
#include "proc/proc.h"

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

// 核心接口
int     mycpuid(void);
cpu_t*  mycpu(void);
proc_t* myproc(void);  // 老师的版本

#endif
