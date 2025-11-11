#ifndef __SYSCALL_ERROR_H__
#define __SYSCALL_ERROR_H__

// 系统调用错误码
#define SYSCALL_SUCCESS     0
#define SYSCALL_EINVAL     -1   // 无效参数
#define SYSCALL_EFAULT     -2   // 内存访问错误
#define SYSCALL_EPERM      -3   // 权限不足
#define SYSCALL_ENOSYS     -4   // 系统调用不存在
#define SYSCALL_ENOMEM     -5   // 内存不足
#define SYSCALL_EBUSY      -6   // 资源忙
#define SYSCALL_ENOENT     -7   // 文件不存在
#define SYSCALL_EMFILE     -8   // 打开文件过多
#define SYSCALL_EISDIR     -9   // 是目录
#define SYSCALL_ENXIO     -10   // 设备不存在
#define SYSCALL_ENFILE    -11   // ✅ 添加：系统文件表满
#define SYSCALL_EBADF     -12   // ✅ 添加：错误的文件描述符
#define SYSCALL_EACCES    -13   // ✅ 添加：权限拒绝
#define SYSCALL_EEXIST    -14   // ✅ 添加：文件已存在
#define SYSCALL_ENOTDIR   -15   // ✅ 添加：不是目录
#define SYSCALL_EAGAIN    -16   // ✅ 添加：资源暂时不可用
#define SYSCALL_ENOSPC    -17   // ✅ 添加：设备空间不足
#define SYSCALL_EROFS     -18   // ✅ 添加：只读文件系统

// 错误处理策略
typedef enum {
    ERROR_RETURN,       // 返回错误码
    ERROR_KILL,         // 杀死进程
    ERROR_PANIC,        // 系统panic
} error_policy_t;

// 错误处理函数
void syscall_error(int error_code, const char* syscall_name);
void set_error_policy(int error_code, error_policy_t policy);
const char* syscall_strerror(int error_code);

#endif
