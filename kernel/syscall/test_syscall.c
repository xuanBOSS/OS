#include "proc/cpu.h"
#include "proc/proc.h"
#include "mem/str.h"
#include "lib/print.h"
#include "syscall/syscall_error.h"
#include "syscall/syscall_args.h"
#include "syscall/syscall.h"

// 测试系统调用 - 验证基本功能
uint64 sys_test_basic()
{
    printf("✅ Basic syscall test passed!\n");
    return 42;
}

// 测试系统调用 - 验证参数传递
uint64 sys_test_args()
{
    uint64 arg0, arg1;
    uint32 arg2;
    
    arg_uint64(0, &arg0);
    arg_uint64(1, &arg1);
    arg_uint32(2, &arg2);
    
    printf("✅ Args test: arg0=%ld, arg1=%ld, arg2=%d\n", arg0, arg1, arg2);
    return arg0 + arg1 + arg2;
}

// 测试系统调用 - 验证错误处理
uint64 sys_test_error()
{
    uint64 mode;
    arg_uint64(0, &mode);
    
    printf("🧪 Error test mode: %ld\n", mode);
    
    switch(mode) {
    case 0:
        printf("✅ Normal return\n");
        return 0;
    case 1:
        printf("❌ Returning EINVAL\n");
        return SYSCALL_EINVAL;
    case 2:
        printf("❌ Returning EFAULT\n");
        return SYSCALL_EFAULT;
    case 3:
        printf("❌ Returning EPERM\n");
        return SYSCALL_EPERM;
    default:
        printf("❌ Unknown error mode\n");
        return SYSCALL_ENOSYS;
    }
}

// 测试系统调用 - 验证权限检查
uint64 sys_test_privilege()
{
    proc_t* p = myproc();
    printf("🔐 Privilege test: current level = %d\n", p->privilege_level);
    
    if (p->privilege_level < 1) {
        printf("❌ Insufficient privilege\n");
        return SYSCALL_EPERM;
    }
    
    printf("✅ Privilege check passed\n");
    return p->privilege_level;
}

// 测试系统调用 - 验证指针验证
uint64 sys_test_pointer()
{
    uint64 ptr;
    uint32 size;
    
    arg_uint64(0, &ptr);
    arg_uint32(1, &size);
    
    printf("🔍 Pointer test: ptr=0x%lx, size=%d\n", ptr, size);
    
    if (!validate_user_ptr(ptr, size)) {
        printf("❌ Invalid user pointer\n");
        return SYSCALL_EFAULT;
    }
    
    printf("✅ Pointer validation passed\n");
    return 0;
}

// 测试系统调用 - 验证字符串处理
uint64 sys_test_string()
{
    char buffer[64];
    uint64 str_ptr;
    
    arg_uint64(0, &str_ptr);
    
    printf("📝 String test: ptr=0x%lx\n", str_ptr);
    
    if (get_user_string(str_ptr, buffer, sizeof(buffer)) != SYSCALL_SUCCESS) {
        printf("❌ Failed to get user string\n");
        return SYSCALL_EFAULT;
    }
    
    printf("✅ Got string: '%s'\n", buffer);
    return strlen(buffer);
}