#ifndef __SYSCALL_ARGS_H__
#define __SYSCALL_ARGS_H__

#include "common.h"
#include "syscall/syscall_table.h"
#include "mem/vmem.h"

// 参数提取结果
typedef struct {
    int error;          // 错误码
    uint64 value;       // 参数值
    void* ptr;          // 指针值（如果是指针类型）
} arg_result_t;

// 核心参数提取函数
int get_syscall_arg(int n, long *arg);
int get_user_string(uint64 user_ptr, char *buf, int max);
int get_user_buffer(uint64 user_ptr, void *buf, int size);

// 类型安全的参数提取函数
arg_result_t extract_int_arg(int n);
arg_result_t extract_uint64_arg(int n);
arg_result_t extract_ptr_arg(int n);
arg_result_t extract_string_arg(int n, char *buf, int max);
arg_result_t extract_buffer_arg(int n, void *buf, int size);

// 参数验证函数
bool validate_user_ptr(uint64 ptr, size_t size);
bool validate_user_string(uint64 ptr, size_t max_len);
bool is_user_accessible(uint64 addr, size_t size, bool write);

int argint(int n, int *ip);
int argaddr(int n, uint64 *ip);
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len);

#endif
