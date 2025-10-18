#include "trap/syscall.h"
#include "trap/exception.h"
#include "riscv.h"
#include "lib/print.h"
#include "mem/str.h"
#include "dev/timer_sched.h"
#include "proc/scheduler.h"
#include "proc/proc.h"

// 全局系统调用统计
struct syscall_stats global_syscall_stats;

// 系统调用名称映射表
static const char* syscall_names[] = {
    [SYS_TEST]    = "sys_test",
    [SYS_PRINT]   = "sys_print", 
    [SYS_YIELD]   = "sys_yield",
    [SYS_GETTIME] = "sys_gettime",
    [SYS_SLEEP]   = "sys_sleep",
    [SYS_GETPID]  = "sys_getpid",
    [SYS_EXIT]    = "sys_exit",
};

// 获取系统调用名称
const char* syscall_name(int syscall_num) {
    if (syscall_num >= 0 && 
        syscall_num < sizeof(syscall_names)/sizeof(syscall_names[0]) && 
        syscall_names[syscall_num]) {
        return syscall_names[syscall_num];
    }
    return "unknown_syscall";
}

// 系统调用初始化
void syscall_init(void) {
    printf("=== System Call Handler Initialization ===\n");
    
    // 清零统计信息
    memset(&global_syscall_stats, 0, sizeof(global_syscall_stats));
    
    printf("System call handler initialized successfully\n");
}

// === 具体系统调用实现 ===

// 测试系统调用
int64 sys_test(uint64 arg0, uint64 arg1, uint64 arg2) {
    printf("CPU %d: sys_test called with args(0x%lx, 0x%lx, 0x%lx)\n", 
           mycpuid(), arg0, arg1, arg2);
    global_syscall_stats.sys_test++;
    return arg0 + arg1 + arg2;
}

// 打印系统调用
int64 sys_print(uint64 message_ptr, uint64 length, uint64 unused) {
    printf("CPU %d: sys_print called - message_ptr=0x%lx, length=%lu\n", 
           mycpuid(), message_ptr, length);
    global_syscall_stats.sys_print++;
    
    printf("User message: [simulated print of %lu bytes]\n", length);
    return length;
}

// 让出CPU系统调用
int64 sys_yield(uint64 unused0, uint64 unused1, uint64 unused2) {
    printf("CPU %d: sys_yield called - voluntarily yielding CPU\n", mycpuid());
    global_syscall_stats.sys_yield++;
    
    yield();
    return 0;
}

// 获取时间系统调用
int64 sys_gettime(uint64 unused0, uint64 unused1, uint64 unused2) {
    uint64 current_time = get_time();
    printf("CPU %d: sys_gettime called - returning time=%lu\n", 
           mycpuid(), current_time);
    global_syscall_stats.sys_gettime++;
    
    return current_time;
}

// 睡眠系统调用
int64 sys_sleep(uint64 duration, uint64 unused1, uint64 unused2) {
    printf("CPU %d: sys_sleep called - sleeping for %lu cycles\n", 
           mycpuid(), duration);
    global_syscall_stats.sys_sleep++;
    
    uint64 start_time = get_time();
    while (get_time() - start_time < duration) {
        for (volatile int i = 0; i < 1000; i++) {
            asm volatile("nop");
        }
    }
    
    printf("CPU %d: sys_sleep completed\n", mycpuid());
    return 0;
}

// 获取进程ID系统调用
int64 sys_getpid(uint64 unused0, uint64 unused1, uint64 unused2) {
    int pid = mycpuid() + 100;
    printf("CPU %d: sys_getpid called - returning PID=%d\n", mycpuid(), pid);
    global_syscall_stats.sys_getpid++;
    
    return pid;
}

// 退出进程系统调用
int64 sys_exit(uint64 exit_code, uint64 unused1, uint64 unused2) {
    printf("CPU %d: sys_exit called with exit_code=%lu\n", mycpuid(), exit_code);
    global_syscall_stats.sys_exit++;
    
    printf("CPU %d: Process exiting (simulated)\n", mycpuid());
    return exit_code;
}

// === 系统调用分发器 ===
int64 syscall_dispatch(int syscall_num, uint64 arg0, uint64 arg1, uint64 arg2) {
    printf("CPU %d: System call %d (%s) dispatched\n", 
           mycpuid(), syscall_num, syscall_name(syscall_num));
    
    global_syscall_stats.total_syscalls++;
    
    switch (syscall_num) {
        case SYS_TEST:
            return sys_test(arg0, arg1, arg2);
        case SYS_PRINT:
            return sys_print(arg0, arg1, arg2);
        case SYS_YIELD:
            return sys_yield(arg0, arg1, arg2);
        case SYS_GETTIME:
            return sys_gettime(arg0, arg1, arg2);
        case SYS_SLEEP:
            return sys_sleep(arg0, arg1, arg2);
        case SYS_GETPID:
            return sys_getpid(arg0, arg1, arg2);
        case SYS_EXIT:
            return sys_exit(arg0, arg1, arg2);
        default:
            printf("CPU %d: Unknown system call number: %d\n", mycpuid(), syscall_num);
            global_syscall_stats.unknown_syscalls++;
            return -1;
    }
}

// === 系统调用主处理函数 ===
void handle_syscall_new(struct trapframe *tf) {
    printf("CPU %d: System call entry\n", mycpuid());
    
    // 简化处理：使用模拟参数进行测试
    int syscall_num = 0;  // 默认测试系统调用
    uint64 arg0 = 100, arg1 = 200, arg2 = 300;
    
    printf("CPU %d: Simulated syscall %d with args(0x%lx, 0x%lx, 0x%lx)\n", 
           mycpuid(), syscall_num, arg0, arg1, arg2);
    
    int64 result = syscall_dispatch(syscall_num, arg0, arg1, arg2);
    
    // 跳过ECALL指令
    tf->sepc += 4;
    
    printf("CPU %d: System call completed - result=%ld\n", mycpuid(), result);
}

// 打印系统调用统计
void print_syscall_stats(void) {
    printf("\n=== System Call Statistics ===\n");
    printf("Total system calls: %lu\n", global_syscall_stats.total_syscalls);
    printf("sys_test: %lu\n", global_syscall_stats.sys_test);
    printf("sys_print: %lu\n", global_syscall_stats.sys_print);
    printf("sys_yield: %lu\n", global_syscall_stats.sys_yield);
    printf("sys_gettime: %lu\n", global_syscall_stats.sys_gettime);
    printf("sys_sleep: %lu\n", global_syscall_stats.sys_sleep);
    printf("sys_getpid: %lu\n", global_syscall_stats.sys_getpid);
    printf("sys_exit: %lu\n", global_syscall_stats.sys_exit);
    printf("Unknown syscalls: %lu\n", global_syscall_stats.unknown_syscalls);
    printf("==============================\n");
}