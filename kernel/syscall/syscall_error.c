#include "syscall/syscall_error.h"
#include "lib/print.h"
#include "proc/cpu.h"

// 错误策略表
static error_policy_t error_policies[8] = {
    [1] = ERROR_RETURN,  // EINVAL
    [2] = ERROR_RETURN,  // EFAULT
    [3] = ERROR_KILL,    // EPERM
    [4] = ERROR_RETURN,  // ENOSYS
    [5] = ERROR_RETURN,  // ENOMEM
    [6] = ERROR_RETURN,  // EBUSY
    [7] = ERROR_RETURN,  // ENOENT
};

void syscall_error(int error_code, const char* syscall_name)
{
    printf("Syscall error in %s: %s (%d)\n", 
           syscall_name, syscall_strerror(error_code), error_code);
    
    error_policy_t policy = error_policies[-error_code];
    
    switch (policy) {
    case ERROR_RETURN:
        // 只返回错误码，不做其他处理
        break;
    case ERROR_KILL:
        printf("Killing process due to syscall error\n");
        // TODO: 实现进程终止
        break;
    case ERROR_PANIC:
        panic("Fatal syscall error");
        break;
    }
}

void set_error_policy(int error_code, error_policy_t policy)
{
    if (error_code > 0 && error_code < 8) {
        error_policies[error_code] = policy;
    }
}

const char* syscall_strerror(int error_code)
{
    switch (error_code) {
    case SYSCALL_SUCCESS: return "Success";
    case SYSCALL_EINVAL:  return "Invalid argument";
    case SYSCALL_EFAULT:  return "Bad address";
    case SYSCALL_EPERM:   return "Operation not permitted";
    case SYSCALL_ENOSYS:  return "Function not implemented";
    case SYSCALL_ENOMEM:  return "Out of memory";
    case SYSCALL_EBUSY:   return "Device or resource busy";
    case SYSCALL_ENOENT:  return "No such file or directory";
    default:              return "Unknown error";
    }
}
