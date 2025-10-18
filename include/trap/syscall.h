#ifndef SYSCALL_H
#define SYSCALL_H

#include "common.h"
#include "trap/trapframe.h"

// 系统调用号定义
#define SYS_TEST        0   // 测试系统调用
#define SYS_PRINT       1   // 打印系统调用
#define SYS_YIELD       2   // 让出CPU
#define SYS_GETTIME     3   // 获取时间
#define SYS_SLEEP       4   // 睡眠
#define SYS_GETPID      5   // 获取进程ID
#define SYS_EXIT        6   // 退出进程

// 系统调用统计结构
struct syscall_stats {
    uint64 total_syscalls;
    uint64 sys_test;
    uint64 sys_print;
    uint64 sys_yield;
    uint64 sys_gettime;
    uint64 sys_sleep;
    uint64 sys_getpid;
    uint64 sys_exit;
    uint64 unknown_syscalls;
};

// 全局变量声明
extern struct syscall_stats global_syscall_stats;

// 函数声明
void syscall_init(void);
int64 syscall_dispatch(int syscall_num, uint64 arg0, uint64 arg1, uint64 arg2);
void handle_syscall_new(struct trapframe *tf);
void print_syscall_stats(void);
const char* syscall_name(int syscall_num);

// 具体系统调用函数声明
int64 sys_test(uint64 arg0, uint64 arg1, uint64 arg2);
int64 sys_print(uint64 message_ptr, uint64 length, uint64 unused);
int64 sys_yield(uint64 unused0, uint64 unused1, uint64 unused2);
int64 sys_gettime(uint64 unused0, uint64 unused1, uint64 unused2);
int64 sys_sleep(uint64 duration, uint64 unused1, uint64 unused2);
int64 sys_getpid(uint64 unused0, uint64 unused1, uint64 unused2);
int64 sys_exit(uint64 exit_code, uint64 unused1, uint64 unused2);

#endif