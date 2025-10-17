#ifndef __CPU_H__
#define __CPU_H__

#include "common.h"

// 声明全局变量
extern volatile int boot_cpu_id;
extern volatile int cpu_started[NCPU];
extern volatile int init_phase;
extern volatile int secondary_cpus_ready;

typedef struct cpu {
    int noff;       // 关中断的深度
    int origin;     // 第一次关中断前的状态
} cpu_t;

int     mycpuid(void);
cpu_t*  mycpu(void);

#endif
