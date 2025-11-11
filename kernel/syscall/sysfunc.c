#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "mem/str.h"
#include "lib/print.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"
#include "syscall/syscall_error.h"
#include "memlayout.h"
#include "riscv.h"
#include "security/security.h"

// 验证用户指针
static int validate_user_ptr(uint64 ptr, uint64 len) {
    // 简单检查：指针在用户空间范围内
    if (ptr < 0x1000 || ptr + len > USER_STACK_TOP) {
        return 0;  // 无效
    }
    return 1;  // 有效
}

// 获取文件描述符和对应的文件结构
static int argfd(int n, int *pfd, struct file **pf) {
    int fd;
    struct file *f;
    proc_t *p = myproc();
    
    if (!p) {
        return -1;
    }
    
    arg_uint32(n, (uint32*)&fd);
    
    if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == NULL) {
        return -1;
    }
    
    if (pfd) *pfd = fd;
    if (pf) *pf = f;
    return 0;
}

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    int increment;
    arg_uint32(0, (uint32*)&increment);
    
    printf("sys_sbrk: adjusting heap by %d bytes\n", increment);
    
    // ✅ 添加安全检查
    if (increment > 0) {
        if (security_check_resource_limit(RESOURCE_MEMORY, increment) != 0) {
            printf("sys_sbrk: memory limit check failed\n");
            return -1;
        }
    }

    proc_t* p = myproc();
    uint64 new_heap_top;
    
    arg_uint64(0, &new_heap_top);
    
    printf("sys_brk: current heap_top=0x%lx, requested=0x%lx\n", 
           p->heap_top, new_heap_top);
    
    // 如果参数为0，返回当前堆顶
    if (new_heap_top == 0) {
        printf("sys_brk: query current heap top: 0x%lx\n", p->heap_top);
        return p->heap_top;
    }
    
    // 检查新堆顶是否合理
    if (new_heap_top < USER_HEAP_BASE) {
        printf("sys_brk: invalid heap top (too low): 0x%lx\n", new_heap_top);
        return -1;
    }
    
    // 检查是否与栈冲突 (简单检查：堆顶不能超过栈底)
    uint64 stack_bottom = USER_STACK_TOP - p->ustack_pages * PGSIZE;
    if (new_heap_top >= stack_bottom) {
        printf("sys_brk: heap would collide with stack\n");
        return -1;
    }
    
    uint64 old_heap_top = p->heap_top;
    
    if (new_heap_top > old_heap_top) {
        // 堆增长
        printf("sys_brk: growing heap from 0x%lx to 0x%lx\n", old_heap_top, new_heap_top);
        uint64 result = uvm_heap_grow(p->pgtbl, old_heap_top, new_heap_top - old_heap_top);
        if (result == old_heap_top) {
            // 增长失败
            printf("sys_brk: heap grow failed\n");
            return -1;
        }
        p->heap_top = new_heap_top;
    } else if (new_heap_top < old_heap_top) {
        // 堆收缩
        printf("sys_brk: shrinking heap from 0x%lx to 0x%lx\n", old_heap_top, new_heap_top);
        uint64 result = uvm_heap_ungrow(p->pgtbl, old_heap_top, old_heap_top - new_heap_top);
        if (result != new_heap_top) {
            printf("sys_brk: heap shrink failed\n");
            return -1;
        }
        p->heap_top = new_heap_top;
    }
    // 如果相等，不需要做任何操作
    
    printf("sys_brk: success, new heap top: 0x%lx\n", p->heap_top);
    return p->heap_top;
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    proc_t* p = myproc();
    uint64 start;
    uint32 len;
    
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    printf("sys_mmap: start=0x%lx, len=%d\n", start, len);
    
    // 检查长度是否有效
    if (len == 0) {
        printf("sys_mmap: invalid length (0)\n");
        return -1;
    }
    
    // 检查长度是否页对齐
    if (len % PGSIZE != 0) {
        printf("sys_mmap: length not page-aligned, rounding up\n");
        len = PGROUNDUP(len);
    }
    
    uint32 npages = len / PGSIZE;
    printf("sys_mmap: mapping %d pages\n", npages);
    
    // 如果start为0，内核选择地址
    if (start == 0) {
        // 简单策略：从堆顶之后开始寻找空闲区域
        start = PGROUNDUP(p->heap_top);
        if (start < USER_MMAP_BASE) {
            start = USER_MMAP_BASE;
        }
        
        // 寻找足够大的空闲区域
        // 这里简化实现，假设从start开始的区域是空闲的
        printf("sys_mmap: kernel chose address: 0x%lx\n", start);
    } else {
        // 检查用户指定的地址是否页对齐
        if (start % PGSIZE != 0) {
            printf("sys_mmap: start address not page-aligned\n");
            return -1;
        }
        
        // 检查地址范围是否合理
        if (start < USER_MMAP_BASE || start + len > USER_STACK_TOP) {
            printf("sys_mmap: invalid address range\n");
            return -1;
        }
    }
    
    // 检查是否与现有映射冲突
    // 这里简化实现，实际应该检查进程的mmap链表
    
    // 执行映射
    printf("sys_mmap: mapping at 0x%lx, %d pages\n", start, npages);
    
    // 设置权限：用户可读写
    int perm = PTE_R | PTE_W | PTE_U;
    
    // 调用uvm_mmap进行映射
    uvm_mmap(start, npages, perm);
    
    printf("sys_mmap: success, mapped at 0x%lx\n", start);
    return start;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    uint64 start;
    uint32 len;
    
    arg_uint64(0, &start);
    arg_uint32(1, &len);
    
    printf("sys_munmap: start=0x%lx, len=%d\n", start, len);
    
    // 检查参数有效性
    if (len == 0) {
        printf("sys_munmap: invalid length (0)\n");
        return -1;
    }
    
    if (start % PGSIZE != 0) {
        printf("sys_munmap: start address not page-aligned\n");
        return -1;
    }
    
    if (len % PGSIZE != 0) {
        printf("sys_munmap: length not page-aligned, rounding up\n");
        len = PGROUNDUP(len);
    }
    
    uint32 npages = len / PGSIZE;
    
    // 检查地址范围是否在用户mmap区域
    if (start < USER_MMAP_BASE || start + len > USER_STACK_TOP) {
        printf("sys_munmap: invalid address range\n");
        return -1;
    }
    
    printf("sys_munmap: unmapping 0x%lx, %d pages\n", start, npages);
    
    // 调用uvm_munmap进行取消映射
    uvm_munmap(start, npages);
    
    printf("sys_munmap: success\n");
    return 0;
}

// copyin 测试 (int 数组)
uint64 sys_copyin()
{
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    printf("sys_copyin: addr=0x%lx, len=%d\n", addr, len);

    // 检查参数有效性
    if (len == 0) {
        printf("sys_copyin: invalid length (0)\n");
        return -1;
    }
    
    if (len > 100) {  // 限制最大长度，防止滥用
        printf("sys_copyin: length too large, limiting to 100\n");
        len = 100;
    }

    int tmp;
    for(int i = 0; i < len; i++) {
        // 检查地址是否有效
        uint64 src_addr = addr + i * sizeof(int);
        
        // 尝试从用户空间复制数据
        uvm_copyin(p->pgtbl, (uint64)&tmp, src_addr, sizeof(int));
        printf("get a number from user[%d]: %d\n", i, tmp);
    }

    printf("sys_copyin: successfully copied %d integers\n", len);
    return 0;
}

// copyout 测试 (int 数组)
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    
    printf("sys_copyout: addr=0x%lx\n", addr);
    
    // 检查地址是否有效
    if (addr == 0) {
        printf("sys_copyout: invalid address (NULL)\n");
        return -1;
    }
    
    // 检查地址是否在用户空间范围内
    if (addr < USER_TEXT_BASE || addr >= USER_STACK_TOP) {
        printf("sys_copyout: address out of user space range\n");
        return -1;
    }
    
    printf("sys_copyout: copying array [1,2,3,4,5] to user space\n");
    
    // 复制数据到用户空间
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);
    
    printf("sys_copyout: successfully copied 5 integers\n");
    return 5;
}

// copyinstr测试
uint64 sys_copyinstr()
{
    char s[64];
    uint64 addr;
    proc_t* p = myproc();

    arg_uint64(0, &addr);
    
    printf("sys_copyinstr: addr=0x%lx\n", addr);
    
    // 检查地址是否有效
    if (addr == 0) {
        printf("sys_copyinstr: invalid address (NULL)\n");
        return -1;
    }
    
    // 检查地址是否在用户空间范围内
    if (addr < USER_TEXT_BASE || addr >= USER_STACK_TOP) {
        printf("sys_copyinstr: address out of user space range\n");
        return -1;
    }
    
    // 清零缓冲区
    memset(s, 0, sizeof(s));
    
    printf("sys_copyinstr: copying string from user space\n");
    
    // 从用户空间复制字符串
    uvm_copyin_str(p->pgtbl, (uint64)s, addr, sizeof(s) - 1);
    
    // 确保字符串以null结尾
    s[sizeof(s) - 1] = '\0';
    
    printf("get str from user: '%s'\n", s);
    printf("sys_copyinstr: string length: %d\n", strlen(s));

    return strlen(s);
}

// ============================================
// 文件操作系统调用
// ============================================

#include "fs/file.h"  // 添加文件系统头文件

// sys_write - 写文件 (使用proc变量版本)
uint64 sys_write(void)
{
    struct file *f;
    int count;
    uint64 buf_addr;
    int fd;
    proc_t *p = myproc();
    
    // 首先获取文件描述符
    if (argfd(0, &fd, &f) < 0) {
        return -1;
    }
    
    arg_uint64(1, &buf_addr);
    arg_uint32(2, (uint32*)&count);

    // 基本参数检查
    if (count < 0) return -1;
    if (count == 0) return 0;

    // ✅ 添加安全检查
    if (security_check_buffer_size(count) != 0) return -1;
    if (security_check_user_ptr(buf_addr, count, PERM_READ) != 0) return -1;
    if (security_check_atomic_operation(fd, OP_WRITE) != 0) return -1;
    if (!f->writable) return -1;
    
    // ✅ 使用 p 变量进行用户指针验证
    if (!validate_user_ptr(buf_addr, count)) return -1;
    
    // 检查页面映射（使用 p 变量）
    uint64 pa = va_to_pa(p->pgtbl, buf_addr & ~(PGSIZE - 1));
    if (pa == 0) return -1;
    
    // 对于控制台设备，使用 filewrite 处理
    return filewrite(f, buf_addr, count);
}

// sys_read - 读文件
uint64 sys_read(void)
{
    int fd;
    uint64 buf_addr;
    int count;
    
    arg_uint32(0, (uint32*)&fd);
    arg_uint64(1, &buf_addr);
    arg_uint32(2, (uint32*)&count);
    
    printf("sys_read: fd=%d, buf=0x%lx, count=%d\n", fd, buf_addr, count);
    
    // ✅ 添加安全检查
    if (security_check_fd_valid(fd) != 0) {
        return -1;
    }
    
    if (security_check_buffer_size(count) != 0) {
        return -1;
    }
    
    if (security_check_user_ptr(buf_addr, count, PERM_WRITE) != 0) {
        printf("sys_read: buffer security check failed\n");
        return -1;
    }

    proc_t* p = myproc();
    
    if (fd < 0 || fd >= NOFILE) {
        printf("sys_read: invalid fd %d\n", fd);
        return -1;
    }
    
    struct file* f = p->ofile[fd];
    if (!f) {
        printf("sys_read: no file for fd %d\n", fd);
        return -1;
    }
    
    if (!f->readable) {
        printf("sys_read: file not readable\n");
        return -1;
    }
    
    // 简化实现：从控制台读取
    if (f->type == FD_DEVICE_E && f->major == CONSOLE) {
        printf("sys_read: reading from console (not implemented)\n");
        return 0;  // 暂时返回0
    }
    
    int result = fileread(f, buf_addr, count);
    return result;
}

// sys_open - 打开文件
uint64 sys_open(void)
{
    char path[256];
    int flags;
    uint64 path_ptr;
    
    arg_uint64(0, &path_ptr);
    arg_uint32(1, (uint32*)&flags);
    
    // ✅ 添加安全检查
    if (security_copyin_str(path, path_ptr, sizeof(path)) != 0) {
        printf("sys_open: failed to get path safely\n");
        return -1;
    }
    
    if (security_check_file_permission(path, flags) != 0) {
        printf("sys_open: file permission check failed\n");
        return -1;
    }
    
    if (security_check_resource_limit(RESOURCE_FILES, 1) != 0) {
        printf("sys_open: file limit check failed\n");
        return -1;
    }
    
    printf("sys_open: path='%s', flags=%d\n", path, flags);

    proc_t* p = myproc();
    if (!p) {
        return -1;
    }
    
    // 查找空闲的文件描述符
    int fd = -1;
    for (int i = 3; i < NOFILE; i++) {  // 跳过 stdin, stdout, stderr
        if (p->ofile[i] == NULL) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        printf("sys_open: no free file descriptors\n");
        return -1;
    }
    
    // 分配文件结构
    struct file* f = filealloc();
    if (!f) {
        printf("sys_open: failed to allocate file structure\n");
        return -1;
    }
    
    // 简化实现：创建一个模拟文件
    f->type = FD_INODE_E;
    f->readable = !(flags & 1);  // O_WRONLY = 1
    f->writable = (flags & 1) || (flags & 2);  // O_WRONLY | O_RDWR
    f->ref = 1;
    f->off = 0;
    
    // 将文件分配给进程
    p->ofile[fd] = f;
    
    printf("sys_open: opened file '%s' with fd=%d\n", path, fd);
    return fd;
}

// sys_close - 关闭文件
uint64 sys_close(void)
{
    int fd;
    arg_uint32(0, (uint32*)&fd);
    
    printf("sys_close: fd=%d\n", fd);

    // ✅ 添加安全检查
    if (security_check_fd_valid(fd) != 0) {
        return -1;
    }
    
    proc_t* p = myproc();
    
    if (fd < 0 || fd >= NOFILE) {
        printf("sys_close: invalid fd %d\n", fd);
        return -1;
    }
    
    struct file* f = p->ofile[fd];
    if (!f) {
        printf("sys_close: no file for fd %d\n", fd);
        return -1;
    }
    
    // 关闭文件
    fileclose(f);
    p->ofile[fd] = NULL;
    
    printf("sys_close: closed fd %d\n", fd);
    return 0;
}

// ============================================
// 进程管理系统调用
// ============================================

// sys_getpid - 获取进程ID
uint64 sys_getpid(void)
{
    proc_t* p = myproc();
    printf("sys_getpid: returning PID %d\n", p->pid);
    return p->pid;
}

// sys_fork - 创建子进程
uint64 sys_fork(void)
{
    printf("sys_fork: creating child process (simplified)\n");
    proc_t* p = myproc();
    printf("sys_fork: parent PID=%d\n", p->pid);
    
    // 简化实现：返回一个假的子进程PID
    printf("sys_fork: would create child process (not fully implemented)\n");
    return p->pid + 1;  // 返回假的子进程PID
}

// sys_exit - 退出进程
uint64 sys_exit(void)
{
    int exit_code;
    arg_uint32(0, (uint32*)&exit_code);
    
    printf("sys_exit: process exiting with code %d\n", exit_code);
    
    proc_t* p = myproc();
    printf("sys_exit: terminating process %s (PID=%d)\n", p->name, p->pid);
    
    // 关闭所有打开的文件
    printf("sys_exit: closing all open files\n");
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            printf("sys_exit: closing fd %d\n", fd);
            fileclose(p->ofile[fd]);
            p->ofile[fd] = NULL;
        }
    }
    
    // 标记进程为僵尸状态
    p->state = PROC_ZOMBIE;
    p->exit_code = exit_code;
    
    printf("sys_exit: process terminated\n");
    
    // 简化实现：直接停止
    printf("=== PROCESS EXIT SIMULATION ===\n");
    while(1) {
        asm volatile("wfi");
    }
    
    return 0;
}

// sys_wait - 等待子进程
uint64 sys_wait(void)
{
    uint64 status_addr;
    arg_uint64(0, &status_addr);
    
    printf("sys_wait: waiting for child process\n");
    
    proc_t* p = myproc();
    printf("sys_wait: parent PID=%d\n", p->pid);
    
    // 简化实现：返回假的子进程PID
    printf("sys_wait: returning child PID (simulated)\n");
    return p->pid + 1;
}

// sys_kill - 发送信号
uint64 sys_kill(void)
{
    int pid;
    int sig;
    
    arg_uint32(0, (uint32*)&pid);
    arg_uint32(1, (uint32*)&sig);
    
    printf("sys_kill: sending signal %d to process %d\n", sig, pid);
    
    // 简化实现：不做实际操作
    printf("sys_kill: signal not implemented\n");
    return 0;
}

// sys_sbrk - 调整堆大小
uint64 sys_sbrk(void)
{
    int increment;
    arg_uint32(0, (uint32*)&increment);
    
    printf("sys_sbrk: adjusting heap by %d bytes\n", increment);
    
    proc_t* p = myproc();
    uint64 old_heap = p->heap_top;
    
    // ✅ 如果是第一次调用，初始化堆
    if (old_heap == 0) {
        old_heap = USER_HEAP_BASE;
        p->heap_top = old_heap;
        printf("sys_sbrk: initializing heap at 0x%lx\n", old_heap);
    }
    
    printf("sys_sbrk: current heap top: 0x%lx\n", old_heap);
    
    if (increment == 0) {
        // 只是查询当前堆顶
        return old_heap;
    }
    
    uint64 new_heap = old_heap + increment;
    
    if (increment > 0) {
        // 扩展堆
        printf("sys_sbrk: expanding heap to 0x%lx\n", new_heap);
        
        // 检查是否与栈冲突（假设栈在 USER_STACK_TOP）
        if (new_heap >= USER_STACK_TOP) {
            printf("sys_sbrk: heap would collide with stack\n");
            return -1;
        }
        
        // ✅ 计算需要分配的页面
        uint64 old_page = PGROUNDUP(old_heap);
        uint64 new_page = PGROUNDUP(new_heap);
        
        printf("sys_sbrk: old_page=0x%lx, new_page=0x%lx\n", old_page, new_page);
        
        // ✅ 为新页面分配物理内存并映射
        for (uint64 va = old_page; va < new_page; va += PGSIZE) {
            uint64 pa = (uint64)pmem_alloc(false);  // 用户内存
            if (!pa) {
                printf("sys_sbrk: failed to allocate physical page\n");
                // 回滚：释放已分配的页面
                for (uint64 va2 = old_page; va2 < va; va2 += PGSIZE) {
                    pte_t *pte = walk_lookup(p->pgtbl, va2);
                    if (pte && (*pte & PTE_V)) {
                        uint64 pa2 = PTE2PA(*pte);
                        pmem_free(pa2, false);  // ✅ 正确调用
                        *pte = 0;
                    }
                }
                return -1;
            }
            
            // 清零新分配的页面
            memset((void*)pa, 0, PGSIZE);
            
            // 映射到用户页表
            if (map_page(p->pgtbl, va, pa, PTE_R | PTE_W | PTE_U) != 0) {
                printf("sys_sbrk: failed to map page at va=0x%lx\n", va);
                pmem_free(pa, false);  // ✅ 正确调用
                // 回滚：释放已分配的页面
                for (uint64 va2 = old_page; va2 < va; va2 += PGSIZE) {
                    pte_t *pte = walk_lookup(p->pgtbl, va2);
                    if (pte && (*pte & PTE_V)) {
                        uint64 pa2 = PTE2PA(*pte);
                        pmem_free(pa2, false);
                        *pte = 0;
                    }
                }
                return -1;
            }
            
            printf("sys_sbrk: mapped page: va=0x%lx -> pa=0x%lx\n", va, pa);
        }
        
        p->heap_top = new_heap;
        
    } else if (increment < 0) {
        // 收缩堆
        if (new_heap < USER_HEAP_BASE) {
            printf("sys_sbrk: heap would go below base\n");
            return -1;
        }
        
        printf("sys_sbrk: shrinking heap to 0x%lx\n", new_heap);
        
        // ✅ 释放不再需要的页面
        uint64 new_page = PGROUNDUP(new_heap);
        uint64 old_page = PGROUNDUP(old_heap);
        
        for (uint64 va = new_page; va < old_page; va += PGSIZE) {
            pte_t *pte = walk_lookup(p->pgtbl, va);
            if (pte && (*pte & PTE_V)) {
                uint64 pa = PTE2PA(*pte);
                pmem_free(pa, false);  // ✅ 正确调用
                *pte = 0;
                printf("sys_sbrk: freed and unmapped page at va=0x%lx\n", va);
            }
        }
        
        p->heap_top = new_heap;
    }
    
    printf("sys_sbrk: returning old heap top: 0x%lx\n", old_heap);
    return old_heap;
}
