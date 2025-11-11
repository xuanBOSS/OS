#include "syscall/syscall_table.h"
#include "syscall/syscall_args.h"
#include "syscall/syscall_error.h"
#include "proc/cpu.h"
#include "lib/print.h"

// 参数验证辅助函数声明
static bool validate_syscall_args(syscall_desc_t* desc);

// 系统调用分发器
void syscall_dispatch(void)
{
    proc_t* p = myproc();
    int syscall_num = p->tf->a7;
    
    // 1. 验证系统调用号
    if (syscall_num < 0 || syscall_num >= syscall_table_size) {
        printf("Invalid syscall number: %d\n", syscall_num);
        p->tf->a0 = SYSCALL_ENOSYS;
        return;
    }
    
    syscall_desc_t* desc = &syscall_table[syscall_num];
    
    // 2. 检查系统调用是否存在
    if (desc->func == NULL) {
        printf("Unimplemented syscall: %s (%d)\n", 
               desc->name ? desc->name : "unknown", syscall_num);
        p->tf->a0 = SYSCALL_ENOSYS;
        return;
    }
    
    // 3. 权限检查
    if (p->privilege_level < desc->min_privilege) {
        printf("Permission denied for syscall: %s\n", desc->name);
        p->tf->a0 = SYSCALL_EPERM;
        syscall_error(SYSCALL_EPERM, desc->name);
        return;
    }
    
    // 4. 参数验证
    if (!validate_syscall_args(desc)) {
        printf("Invalid arguments for syscall: %s\n", desc->name);
        p->tf->a0 = SYSCALL_EINVAL;
        syscall_error(SYSCALL_EINVAL, desc->name);
        return;
    }
    
    // 5. 调用系统调用实现
    uint64 result = desc->func();
    p->tf->a0 = result;
    
    // 6. 调试信息（可选）
    #ifdef SYSCALL_DEBUG
    printf("Syscall %s returned: %ld\n", desc->name, result);
    #endif
}


// 参数验证辅助函数
static bool validate_syscall_args(syscall_desc_t* desc)
{
    for (int i = 0; i < desc->arg_count; i++) {
        arg_desc_t* arg = &desc->args[i];
        long arg_value;
        
        // ✅ 使用现有的函数签名
        if (get_syscall_arg(i, &arg_value) != 0) {
            printf("Failed to get argument %d\n", i);
            return false;
        }
        
        switch (arg->type) {
        case ARG_PTR:
        case ARG_STRING:
        case ARG_BUFFER:
            if (arg_value == 0 && !arg->nullable) {
                printf("Argument %d: NULL pointer not allowed\n", i);
                return false;  // NULL指针但不允许为NULL
            }
            if (arg_value != 0 && !validate_user_ptr((uint64)arg_value, arg->size)) {
                printf("Argument %d: invalid user pointer 0x%lx\n", i, arg_value);
                return false;  // 无效的用户指针
            }
            break;
        case ARG_INT:
        case ARG_UINT64:
            // 整数参数通常不需要特殊验证
            printf("Argument %d: integer value %ld\n", i, arg_value);
            break;
        default:
            printf("Argument %d: unknown type %d\n", i, arg->type);
            break;
        }
    }
    return true;
}
