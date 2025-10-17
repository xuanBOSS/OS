#include "proc/proc.h"
#include "riscv.h"

volatile int boot_cpu_id = -1;
volatile int cpu_started[NCPU] = {0};
volatile int init_phase = 0;
volatile int secondary_cpus_ready = 0;

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
