#include "lib/print.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "proc/cpu.h"        // 添加这个头文件，包含 myproc 和 mycpuid
#include "dev/timer.h"
#include "riscv.h"

// 系统调用号定义
#define SYS_print     0   // 打印系统调用
#define SYS_gettime   1   // 获取时间
#define SYS_getpid    2   // 获取进程ID
#define SYS_exit      3   // 退出进程

// 系统调用初始化
void syscall_init(void) {
    printf("System call interface initialized\n");
}
