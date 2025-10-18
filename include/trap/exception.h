#ifndef EXCEPTION_H
#define EXCEPTION_H

#include "common.h"
#include "trap/trapframe.h"

// RISC-V 异常原因码定义
#define CAUSE_INST_MISALIGNED     0   
#define CAUSE_INST_ACCESS_FAULT   1   
#define CAUSE_ILLEGAL_INST        2   
#define CAUSE_BREAKPOINT          3   
#define CAUSE_LOAD_MISALIGNED     4   
#define CAUSE_LOAD_ACCESS_FAULT   5   
#define CAUSE_STORE_MISALIGNED    6   
#define CAUSE_STORE_ACCESS_FAULT  7   
#define CAUSE_ECALL_U             8   
#define CAUSE_ECALL_S             9   
#define CAUSE_INST_PAGE_FAULT    12   
#define CAUSE_LOAD_PAGE_FAULT    13   
#define CAUSE_STORE_PAGE_FAULT   15   

// 异常统计结构
struct exception_stats {
    uint64 total_exceptions;
    uint64 inst_misaligned;
    uint64 inst_access_fault;
    uint64 illegal_inst;
    uint64 breakpoint;
    uint64 load_misaligned;
    uint64 load_access_fault;
    uint64 store_misaligned;
    uint64 store_access_fault;
    uint64 ecall_u;
    uint64 ecall_s;
    uint64 inst_page_fault;
    uint64 load_page_fault;
    uint64 store_page_fault;
    uint64 unknown_exceptions;
};

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

// 全局异常统计
extern struct exception_stats global_exception_stats;
extern struct syscall_stats global_syscall_stats;

// 异常处理函数声明
void exception_init(void);
void handle_exception_new(struct trapframe *tf);  // 新的异常处理器

// 具体异常处理函数
void handle_inst_misaligned_new(struct trapframe *tf);
void handle_inst_access_fault_new(struct trapframe *tf);
void handle_illegal_inst_new(struct trapframe *tf);
void handle_breakpoint_new(struct trapframe *tf);
void handle_load_misaligned_new(struct trapframe *tf);
void handle_load_access_fault_new(struct trapframe *tf);
void handle_store_misaligned_new(struct trapframe *tf);
void handle_store_access_fault_new(struct trapframe *tf);
void handle_syscall_new(struct trapframe *tf);
void handle_ecall_s_new(struct trapframe *tf);
void handle_instruction_page_fault_new(struct trapframe *tf);
void handle_load_page_fault_new(struct trapframe *tf);
void handle_store_page_fault_new(struct trapframe *tf);

// 系统调用处理
void syscall_init(void);
int64 syscall_dispatch(int syscall_num, uint64 arg0, uint64 arg1, uint64 arg2);

// 辅助函数
void print_exception_stats(void);
void print_syscall_stats(void);
void print_trapframe_new(struct trapframe *tf);
const char* exception_name(uint64 cause);

// 测试函数
void test_exception_modules(void);

#endif
