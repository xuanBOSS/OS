#include "proc/proc.h"
#include "riscv.h"

// 静态初始化为0，避免未初始化问题
static cpu_t cpus[NCPU];

cpu_t* mycpu(void)
{
    int cpuid = mycpuid();
    if (cpuid >= NCPU) {
        return &cpus[0];
    }
    
    // 确保cpu结构体已初始化
    if (cpus[cpuid].noff == 0 && cpus[cpuid].origin == 0) {
        cpus[cpuid].noff = 0;
        cpus[cpuid].origin = 0;
    }
    
    return &cpus[cpuid];
}

int mycpuid(void) 
{
    int id;
    asm volatile("csrr %0, mhartid" : "=r" (id));
    return id;
}