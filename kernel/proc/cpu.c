#include "proc/cpu.h"
#include "riscv.h"

// 保留你的多CPU管理变量
volatile int boot_cpu_id = -1;
volatile int cpu_started[NCPU] = {0};
volatile int init_phase = 0;
volatile int secondary_cpus_ready = 0;

// CPU数组
static cpu_t cpus[NCPU];

// ✅ 添加初始化标记数组
static int cpu_initialized[NCPU] = {0};

// 实现老师要求的函数
cpu_t* mycpu(void)
{
    int cpuid = mycpuid();
    if (cpuid >= NCPU) {
        return &cpus[0];  // 防错处理
    }
    
    // ✅ 只在第一次访问时初始化
    if (!cpu_initialized[cpuid]) {
        cpus[cpuid].noff = 0;
        cpus[cpuid].origin = 0;
        cpus[cpuid].proc = 0;
        cpu_initialized[cpuid] = 1;  // 标记已初始化
    }
    
    return &cpus[cpuid];
}

int mycpuid(void) 
{
    // 在 S-mode 中不能访问 mhartid，使用 tp 寄存器
    // tp 寄存器在 start() 函数中已经设置为 hart ID
    return r_tp();
}

// 实现老师要求的myproc函数
proc_t* myproc(void)
{
    cpu_t* cpu = mycpu();
    return cpu->proc;
}
