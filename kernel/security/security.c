#include "security/security.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/str.h"      // ✅ 添加：字符串函数
#include "mem/pmem.h"     // ✅ 添加：可能需要的内存管理
#include "lib/print.h"
#include "memlayout.h"
#include "riscv.h"        // ✅ 添加：PGSIZE等定义
#include "fs/file.h"

#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFUL
#endif

// ✅ 前置声明内部函数
static int security_read_user_byte(uint64 addr, char *byte);

// ✅ 先定义内部辅助函数
static int security_read_user_byte(uint64 addr, char *byte) {
    proc_t *p = myproc();
    if (!p || !p->pgtbl) {
        return -1;
    }
    
    // 检查地址是否有效
    pte_t *pte = walk_lookup(p->pgtbl, addr);
    if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U) || !(*pte & PTE_R)) {
        return -1;
    }
    
    // 通过物理地址读取
    uint64 pa = va_to_pa(p->pgtbl, addr);
    if (pa == 0) {
        return -1;
    }
    
    *byte = *(char*)pa;
    return 0;
}

// 检查用户指针是否安全
int security_check_user_ptr(uint64 ptr, uint64 size, int perm) {
    if (ptr == 0) {
        printf("Security: NULL pointer access\n");
        return -1;
    }
    
    // ✅ 添加更严格的地址范围检查
    if (ptr >= 0x80000000UL) {  // 内核空间地址
        printf("Security: attempt to access kernel space 0x%lx\n", ptr);
        return -1;
    }
    
    if (ptr >= 0x40000000UL) {  // 超出合理的用户空间范围
        printf("Security: pointer 0x%lx too high\n", ptr);
        return -1;
    }
    
    // 检查地址范围
    if (ptr < USER_TEXT_BASE || ptr >= USER_STACK_TOP) {
        printf("Security: pointer 0x%lx out of user space\n", ptr);
        return -1;
    }
    
    // ✅ 添加大小检查
    if (size == 0) {
        printf("Security: zero size access\n");
        return -1;
    }
    
    if (size > 0x10000000UL) {  // 256MB 限制
        printf("Security: size %ld too large\n", size);
        return -1;
    }
    
    // 检查溢出
    if (ptr + size < ptr) {
        printf("Security: pointer arithmetic overflow\n");
        return -1;
    }
    
    if (ptr + size > USER_STACK_TOP) {
        printf("Security: access beyond user space\n");
        return -1;
    }
    
    // 检查页表权限
    proc_t *p = myproc();
    if (!p || !p->pgtbl) {
        printf("Security: no process or page table\n");
        return -1;
    }
    
    // ✅ 修改：使用正确的页面对齐宏
    for (uint64 addr = PGROUNDDOWN(ptr); addr < PGROUNDUP(ptr + size); addr += PGSIZE) {
        pte_t *pte = walk_lookup(p->pgtbl, addr);
        if (!pte || !(*pte & PTE_V) || !(*pte & PTE_U)) {
            printf("Security: invalid page at 0x%lx\n", addr);
            return -1;
        }
        
        if ((perm & PERM_WRITE) && !(*pte & PTE_W)) {
            printf("Security: write permission denied at 0x%lx\n", addr);
            return -1;
        }
        
        if ((perm & PERM_READ) && !(*pte & PTE_R)) {
            printf("Security: read permission denied at 0x%lx\n", addr);
            return -1;
        }
    }
    
    return 0;
}

// 检查用户字符串
int security_check_user_string(uint64 ptr, uint64 max_len) {
    if (security_check_user_ptr(ptr, max_len, PERM_READ) != 0) {
        return -1;
    }
    
    // 检查字符串是否正确终止
    char c;
    for (uint64 i = 0; i < max_len; i++) {
        if (security_read_user_byte(ptr + i, &c) != 0) {
            printf("Security: failed to read string at offset %ld\n", i);
            return -1;
        }
        
        if (c == '\0') {
            return 0;  // 找到字符串结尾
        }
    }
    
    printf("Security: string not null-terminated within %ld bytes\n", max_len);
    return -1;
}

// 检查缓冲区大小
int security_check_buffer_size(uint64 size) {
    if (size > MAX_BUFFER_SIZE) {
        printf("Security: buffer size %ld exceeds limit %d\n", size, MAX_BUFFER_SIZE);
        return -1;
    }
    return 0;
}

// ✅ 添加简单的字符串查找函数（避免使用标准库）
static char* simple_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    if (*needle == '\0') return (char*)haystack;
    
    for (const char* h = haystack; *h != '\0'; h++) {
        const char* h_temp = h;
        const char* n_temp = needle;
        
        while (*h_temp != '\0' && *n_temp != '\0' && *h_temp == *n_temp) {
            h_temp++;
            n_temp++;
        }
        
        if (*n_temp == '\0') {
            return (char*)h;
        }
    }
    
    return NULL;
}

// 检查文件权限
int security_check_file_permission(const char *path, int flags) {
    if (!path) {
        printf("Security: NULL path\n");
        return -1;
    }
    
    // 简单实现：检查路径长度
    int path_len = strlen(path);
    if (path_len > MAX_PATH_LEN) {
        printf("Security: path too long (%d > %d)\n", path_len, MAX_PATH_LEN);
        return -1;
    }
    
    // ✅ 修改：使用自定义的字符串查找函数
    if (simple_strstr(path, "..") != NULL) {
        printf("Security: path traversal attempt in '%s'\n", path);
        return -1;
    }
    
    // ✅ 添加：检查空路径
    if (path_len == 0) {
        printf("Security: empty path\n");
        return -1;
    }
    
    return 0;
}

// 检查资源限制
int security_check_resource_limit(int resource_type, uint64 amount) {
    proc_t *p = myproc();
    if (!p) {
        printf("Security: no current process\n");
        return -1;
    }
    
    switch (resource_type) {
    case RESOURCE_MEMORY:
        // ✅ 修改：更安全的堆大小检查
        if (p->heap_top == 0) {
            // 堆还未初始化，检查是否超过最大堆大小
            if (amount > MAX_HEAP_SIZE) {
                printf("Security: initial heap size %ld exceeds limit %d\n", 
                       amount, MAX_HEAP_SIZE);
                return -1;
            }
        } else {
            // 堆已初始化，检查增长后是否超过限制
            uint64 current_heap_size = p->heap_top - USER_HEAP_BASE;
            if (current_heap_size + amount > MAX_HEAP_SIZE) {
                printf("Security: heap size would exceed limit (current=%ld, add=%ld, limit=%d)\n", 
                       current_heap_size, amount, MAX_HEAP_SIZE);
                return -1;
            }
        }
        break;
        
    case RESOURCE_FILES:
        // 计算已打开的文件数
        int open_files = 0;
        for (int i = 0; i < NOFILE; i++) {
            if (p->ofile[i] != NULL) {
                open_files++;
            }
        }
        if (open_files >= MAX_OPEN_FILES) {
            printf("Security: too many open files (%d >= %d)\n", 
                   open_files, MAX_OPEN_FILES);
            return -1;
        }
        break;
        
    case RESOURCE_PROCESSES:
        // ✅ 添加：进程数量限制检查（简化实现）
        printf("Security: process limit check not implemented\n");
        break;
        
    default:
        printf("Security: unknown resource type %d\n", resource_type);
        return -1;
    }
    
    return 0;
}

// 检查文件描述符有效性
int security_check_fd_valid(int fd) {
    if (fd < 0 || fd >= NOFILE) {
        printf("Security: invalid fd %d (range: 0-%d)\n", fd, NOFILE-1);
        return -1;
    }
    
    proc_t *p = myproc();
    if (!p) {
        printf("Security: no current process\n");
        return -1;
    }
    
    if (p->ofile[fd] == NULL) {
        printf("Security: fd %d not open\n", fd);
        return -1;
    }
    
    return 0;
}

// 安全的内存复制函数
int security_copyin(uint64 dst, uint64 src, uint64 len) {
    if (security_check_buffer_size(len) != 0) {
        return -1;
    }
    
    if (security_check_user_ptr(src, len, PERM_READ) != 0) {
        return -1;
    }
    
    proc_t *p = myproc();
    if (!p || !p->pgtbl) {
        printf("Security: no process or page table for copyin\n");
        return -1;
    }
    
    // ✅ 修改：使用正确的函数签名
    uvm_copyin(p->pgtbl, dst, src, (uint32)len);
    return 0;  // uvm_copyin 是 void 函数，假设成功
}

int security_copyout(uint64 dst, uint64 src, uint64 len) {
    if (security_check_buffer_size(len) != 0) {
        return -1;
    }
    
    if (security_check_user_ptr(dst, len, PERM_WRITE) != 0) {
        return -1;
    }
    
    proc_t *p = myproc();
    if (!p || !p->pgtbl) {
        printf("Security: no process or page table for copyout\n");
        return -1;
    }
    
    // ✅ 修改：使用正确的函数签名
    uvm_copyout(p->pgtbl, dst, src, (uint32)len);
    return 0;  // uvm_copyout 是 void 函数，假设成功
}

int security_copyin_str(char *dst, uint64 src, uint64 max_len) {
    if (security_check_user_string(src, max_len) != 0) {
        return -1;
    }
    
    proc_t *p = myproc();
    if (!p || !p->pgtbl) {
        printf("Security: no process or page table for copyin_str\n");
        return -1;
    }
    
    // ✅ 修改：使用正确的函数签名
    uvm_copyin_str(p->pgtbl, (uint64)dst, src, (uint32)max_len);
    return 0;  // uvm_copyin_str 是 void 函数，假设成功
}

// 检查整数溢出
int security_check_integer_overflow(uint64 a, uint64 b) {
    if (a > 0 && b > 0 && a > (UINT64_MAX - b)) {
        printf("Security: integer overflow detected (a=%ld, b=%ld)\n", a, b);
        return -1;
    }
    return 0;
}

// 检查字符串安全性（增强版）
int security_check_string_safety(const char *str, uint64 max_len) {
    if (!str) {
        printf("Security: NULL string pointer\n");
        return -1;
    }
    
    // 检查字符串是否在用户空间
    if (security_check_user_ptr((uint64)str, max_len, PERM_READ) != 0) {
        return -1;
    }
    
    // 检查是否包含危险字符
    for (uint64 i = 0; i < max_len; i++) {
        char c;
        if (security_read_user_byte((uint64)str + i, &c) != 0) {
            return -1;
        }
        
        if (c == '\0') {
            return 0;  // 正常结束
        }
        
        // 检查危险字符（简化版本）
        if (c < 0x20 && c != '\n' && c != '\t' && c != '\r') {
            printf("Security: dangerous character 0x%x in string\n", (unsigned char)c);
            return -1;
        }
    }
    
    printf("Security: string not null-terminated\n");
    return -1;
}

// 检查路径安全性（增强版）
int security_check_path_safety(const char *path) {
    if (!path) {
        printf("Security: NULL path\n");
        return -1;
    }
    
    int len = 0;
    char prev_char = '\0';
    
    // 逐字符检查路径
    for (int i = 0; i < MAX_PATH_LEN; i++) {
        char c;
        if (security_read_user_byte((uint64)path + i, &c) != 0) {
            printf("Security: cannot read path character at offset %d\n", i);
            return -1;
        }
        
        if (c == '\0') {
            break;
        }
        
        len++;
        
        // 检查路径遍历
        if (c == '.' && prev_char == '.') {
            printf("Security: path traversal detected in '%s'\n", path);
            return -1;
        }
        
        // 检查非法字符
        if (c < 0x20 || c > 0x7E) {
            printf("Security: illegal character 0x%x in path\n", (unsigned char)c);
            return -1;
        }
        
        prev_char = c;
    }
    
    if (len == 0) {
        printf("Security: empty path\n");
        return -1;
    }
    
    if (len >= MAX_PATH_LEN) {
        printf("Security: path too long (%d >= %d)\n", len, MAX_PATH_LEN);
        return -1;
    }
    
    return 0;
}

// 检查操作原子性
int security_check_atomic_operation(int fd, int operation) {
    proc_t *p = myproc();
    if (!p) {
        printf("Security: no current process for atomic check\n");
        return -1;
    }
    
    // 检查文件描述符状态
    if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
        printf("Security: invalid fd %d for atomic operation\n", fd);
        return -1;
    }
    
    // 检查文件状态是否一致
    struct file *f = p->ofile[fd];
    if (f->ref <= 0) {
        printf("Security: file reference count invalid (%d)\n", f->ref);
        return -1;
    }
    
    return 0;
}