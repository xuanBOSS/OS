#ifndef SECURITY_H
#define SECURITY_H

#include "common.h"

// 安全检查常量
#define MAX_STRING_LEN      4096
#define MAX_BUFFER_SIZE     (1024 * 1024)  // 1MB
#define MAX_PATH_LEN        256
#define MAX_OPEN_FILES      16
#define MAX_HEAP_SIZE       (64 * 1024 * 1024)  // 64MB

// 权限标志
#define PERM_READ   0x1
#define PERM_WRITE  0x2
#define PERM_EXEC   0x4

// 资源类型
#define RESOURCE_MEMORY     1
#define RESOURCE_FILES      2
#define RESOURCE_PROCESSES  3

// 操作类型（用于原子性检查）
#define OP_READ     1
#define OP_WRITE    2
#define OP_CLOSE    3

// 基本安全检查函数
int security_check_user_ptr(uint64 ptr, uint64 size, int perm);
int security_check_user_string(uint64 ptr, uint64 max_len);
int security_check_buffer_size(uint64 size);
int security_check_file_permission(const char *path, int flags);
int security_check_resource_limit(int resource_type, uint64 amount);
int security_check_fd_valid(int fd);

// 增强安全检查函数
int security_check_integer_overflow(uint64 a, uint64 b);
int security_check_string_safety(const char *str, uint64 max_len);
int security_check_path_safety(const char *path);
int security_check_atomic_operation(int fd, int operation);

// 安全的内存操作
int security_copyin(uint64 dst, uint64 src, uint64 len);
int security_copyout(uint64 dst, uint64 src, uint64 len);
int security_copyin_str(char *dst, uint64 src, uint64 max_len);
#endif
