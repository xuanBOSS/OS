#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "common.h"

// 系统调用主处理函数
void syscall(void);
void syscall_dispatch(void);  // 新增：高级分发器

// 基于参数寄存器编号的读取（保持兼容性）
void arg_uint32(int n, uint32* ip);
void arg_uint64(int n, uint64* ip);
void arg_str(int n, char* buf, int maxlen);

// 新增：高级参数提取接口
int get_syscall_arg(int n, long *arg);
int get_user_string(uint64 user_ptr, char *buf, int max);
int get_user_buffer(uint64 user_ptr, void *buf, int size);

#endif
