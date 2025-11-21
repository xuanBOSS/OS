#include "proc/cpu.h"
#include "riscv.h"
#include "lib/print.h"

// 保留你的多CPU管理变量
volatile int boot_cpu_id = -1;
volatile int cpu_started[NCPU] = {0};
volatile int init_phase = 0;
volatile int secondary_cpus_ready = 0;

// CPU数组 - 移除 static，与头文件中的 extern 声明匹配
cpu_t cpus[NCPU];  // ✅ 修复：移除 static 关键字

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
        cpus[cpuid].intena = 0;  
        cpus[cpuid].proc = NULL;  // ✅ 使用 NULL 而不是 0
        // 初始化 context
        cpus[cpuid].ctx.ra = 0;
        cpus[cpuid].ctx.sp = 0;
        cpus[cpuid].ctx.s0 = 0;
        cpus[cpuid].ctx.s1 = 0;
        cpus[cpuid].ctx.s2 = 0;
        cpus[cpuid].ctx.s3 = 0;
        cpus[cpuid].ctx.s4 = 0;
        cpus[cpuid].ctx.s5 = 0;
        cpus[cpuid].ctx.s6 = 0;
        cpus[cpuid].ctx.s7 = 0;
        cpus[cpuid].ctx.s8 = 0;
        cpus[cpuid].ctx.s9 = 0;
        cpus[cpuid].ctx.s10 = 0;
        cpus[cpuid].ctx.s11 = 0;
        
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

// ✅ 添加CPU初始化函数
void cpu_init(void)
{
    printf("=== CPU Initialization ===\n");
    
    // 初始化所有CPU结构
    for (int i = 0; i < NCPU; i++) {
        cpus[i].noff = 0;
        cpus[i].origin = 0;
        cpus[i].intena = 0;
        cpus[i].proc = NULL;
        
        // 初始化 context
        cpus[i].ctx.ra = 0;
        cpus[i].ctx.sp = 0;
        cpus[i].ctx.s0 = 0;
        cpus[i].ctx.s1 = 0;
        cpus[i].ctx.s2 = 0;
        cpus[i].ctx.s3 = 0;
        cpus[i].ctx.s4 = 0;
        cpus[i].ctx.s5 = 0;
        cpus[i].ctx.s6 = 0;
        cpus[i].ctx.s7 = 0;
        cpus[i].ctx.s8 = 0;
        cpus[i].ctx.s9 = 0;
        cpus[i].ctx.s10 = 0;
        cpus[i].ctx.s11 = 0;
        
        cpu_initialized[i] = 1;
    }
    
    printf("Initialized %d CPU structures\n", NCPU);
    printf("Current CPU ID: %d\n", mycpuid());
    printf("==========================\n");
}

// ✅ 添加设置当前进程的函数
void set_current_proc(proc_t* proc)
{
    cpu_t* cpu = mycpu();
    cpu->proc = proc;
    printf("CPU %d: set current process to %s (PID=%d)\n", 
           mycpuid(), 
           proc ? proc->name : "NULL", 
           proc ? proc->pid : -1);
}

// ✅ 添加获取CPU统计信息的函数
void cpu_stats(void)
{
    printf("\n=== CPU Statistics ===\n");
    for (int i = 0; i < NCPU; i++) {
        printf("CPU %d:\n", i);
        printf("  noff: %d\n", cpus[i].noff);
        printf("  origin: %d\n", cpus[i].origin);
        printf("  intena: %d\n", cpus[i].intena);
        printf("  proc: %s (PID=%d)\n", 
               cpus[i].proc ? cpus[i].proc->name : "NULL",
               cpus[i].proc ? cpus[i].proc->pid : -1);
        printf("  initialized: %s\n", cpu_initialized[i] ? "yes" : "no");
    }
    printf("======================\n");
}
