#ifndef __SYSCALL_TABLE_H__
#define __SYSCALL_TABLE_H__

#include "common.h"

// 参数类型枚举
typedef enum {
    ARG_INT,        // 整数参数
    ARG_UINT64,     // 64位无符号整数
    ARG_PTR,        // 指针参数
    ARG_STRING,     // 字符串参数
    ARG_BUFFER,     // 缓冲区参数
} arg_type_t;

// 参数描述结构
typedef struct {
    arg_type_t type;    // 参数类型
    int size;           // 参数大小（对于缓冲区）
    bool nullable;      // 是否允许为NULL
} arg_desc_t;

// 系统调用描述符
typedef struct syscall_desc {
    uint64 (*func)(void);       // 实现函数指针
    const char *name;           // 系统调用名称
    int arg_count;              // 参数个数
    arg_desc_t args[6];         // 参数描述（最多6个参数）
    bool need_proc;             // 是否需要进程上下文
    int min_privilege;          // 最小权限级别
} syscall_desc_t;

// 系统调用表
extern syscall_desc_t syscall_table[];
extern const int syscall_table_size;

// 宏定义简化系统调用注册
#define SYSCALL_ENTRY(num, func_name, name_str, argc, ...) \
    [num] = { \
        .func = (uint64(*)(void))func_name, \
        .name = name_str, \
        .arg_count = argc, \
        .args = {__VA_ARGS__}, \
        .need_proc = true, \
        .min_privilege = 0 \
    }

#endif
