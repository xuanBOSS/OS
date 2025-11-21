#include "syscall/syscall_args.h"
#include "syscall/syscall_error.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/str.h"
#include "lib/print.h"
#include "security/security.h"

// 基础参数提取
int get_syscall_arg(int n, long *arg)
{
    if (n < 0 || n > 5) {
        return SYSCALL_EINVAL;
    }
    
    proc_t* p = myproc();
    switch (n) {
    case 0: *arg = p->tf->a0; break;
    case 1: *arg = p->tf->a1; break;
    case 2: *arg = p->tf->a2; break;
    case 3: *arg = p->tf->a3; break;
    case 4: *arg = p->tf->a4; break;
    case 5: *arg = p->tf->a5; break;
    }
    return SYSCALL_SUCCESS;
}

// 用户字符串提取
int get_user_string(uint64 user_ptr, char *buf, int max)
{
    if (!validate_user_string(user_ptr, max)) {
        return SYSCALL_EFAULT;
    }
    
    proc_t* p = myproc();
    uvm_copyin_str(p->pgtbl, (uint64)buf, user_ptr, max);
    return SYSCALL_SUCCESS;
}

// 用户缓冲区提取
int get_user_buffer(uint64 user_ptr, void *buf, int size)
{
    if (!validate_user_ptr(user_ptr, size)) {
        return SYSCALL_EFAULT;
    }
    
    proc_t* p = myproc();
    uvm_copyin(p->pgtbl, (uint64)buf, user_ptr, size);
    return SYSCALL_SUCCESS;
}

// 指针验证
bool validate_user_ptr(uint64 ptr, size_t size)
{
    if (ptr == 0) return false;  // NULL指针
    if (ptr >= VA_MAX) return false;  // 超出虚拟地址空间
    if (ptr + size < ptr) return false;  // 溢出检查
    
    proc_t* p = myproc();
    
    // 检查地址范围是否在用户空间
    for (uint64 addr = ptr; addr < ptr + size; addr += PGSIZE) {
        pte_t* pte = walk_lookup(p->pgtbl, addr);
        if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) {
            return false;
        }
    }
    return true;
}

// 字符串验证
bool validate_user_string(uint64 ptr, size_t max_len)
{
    if (!validate_user_ptr(ptr, 1)) return false;
    
    // 简单实现：检查前max_len字节是否可访问
    return validate_user_ptr(ptr, max_len);
}

// 类型安全的参数提取函数
arg_result_t extract_int_arg(int n)
{
    arg_result_t result = {0};
    long arg;
    result.error = get_syscall_arg(n, &arg);
    result.value = (uint64)arg;
    return result;
}

arg_result_t extract_uint64_arg(int n)
{
    return extract_int_arg(n);  // 相同实现
}

arg_result_t extract_ptr_arg(int n)
{
    arg_result_t result = extract_int_arg(n);
    result.ptr = (void*)result.value;
    return result;
}

arg_result_t extract_string_arg(int n, char *buf, int max)
{
    arg_result_t result = extract_ptr_arg(n);
    if (result.error == SYSCALL_SUCCESS) {
        result.error = get_user_string(result.value, buf, max);
    }
    return result;
}

arg_result_t extract_buffer_arg(int n, void *buf, int size)
{
    arg_result_t result = extract_ptr_arg(n);
    if (result.error == SYSCALL_SUCCESS) {
        result.error = get_user_buffer(result.value, buf, size);
    }
    return result;
}

bool is_user_accessible(uint64 addr, size_t size, bool write)
{
    proc_t* p = myproc();
    
    for (uint64 a = addr; a < addr + size; a += PGSIZE) {
        pte_t* pte = walk_lookup(p->pgtbl, a);
        if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) {
            return false;
        }
        if (write && !(*pte & PTE_W)) {
            return false;
        }
    }
    return true;
}

int argint(int n, int *ip) {
    long arg;
    int result = get_syscall_arg(n, &arg);
    if (result == SYSCALL_SUCCESS) {
        *ip = (int)arg;
    }
    return result;
}

int argaddr(int n, uint64 *ip) {
    long arg;
    int result = get_syscall_arg(n, &arg);
    if (result == SYSCALL_SUCCESS) {
        *ip = (uint64)arg;
    }
    return result;
}

int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    if (!pagetable || !src) {
        return -1;  // 参数错误
    }
    
    // 简单验证目标地址是否可访问
    if (va_to_pa(pagetable, dstva) == 0) {
        return -1;  // 目标页面未映射
    }
    
    // 调用 uvm_copyout
    uvm_copyout(pagetable, dstva, (uint64)src, len);
    
    return 0;  // 假设成功
}
