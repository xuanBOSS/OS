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
#include "trap/trap.h"
#include "fs/dir.h"    
#include "fs/inode.h"  
#include "fs/stat.h"
#include "fs/fcntl.h"
#include "fs/log.h" 

static int get_user_string(uint64 src, char* dst, int max_len)
{
    proc_t* p = myproc();
    
    for (int i = 0; i < max_len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&dst[i], src + i, 1);
        
        if (dst[i] == '\0') {
            return 0;
        }
    }
    
    // 超过最大长度，强制终止
    dst[max_len - 1] = '\0';
    return 0;
}

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
    
    // ✅ 只对普通文件添加事务（设备文件不需要）
    int ret;
    if (f->type == FD_FILE) {
        begin_op();
        ret = file_write(f, count, buf_addr, true);
        end_op();
    } else {
        // 控制台等设备文件不需要事务
        ret = file_write(f, count, buf_addr, true);
    }
    
    return ret;
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
    
    // printf("sys_read: fd=%d, buf_addr=0x%lx, count=%d\n", fd, buf_addr, count);
    
    // ✅ 添加安全检查
    if (security_check_fd_valid(fd) != 0) {
        // printf("sys_read: security_check_fd_valid failed\n");
        return -1;
    }
    
    if (security_check_buffer_size(count) != 0) {
        // printf("sys_read: security_check_buffer_size failed\n");
        return -1;
    }
    
    if (security_check_user_ptr(buf_addr, count, PERM_WRITE) != 0) {
        // printf("sys_read: security_check_user_ptr failed (buf_addr=0x%lx, count=%d)\n", buf_addr, count);
        return -1;
    }

    proc_t* p = myproc();
    
    if (fd < 0 || fd >= NOFILE) {
        // printf("sys_read: invalid fd=%d\n", fd);
        return -1;
    }
    
    struct file* f = p->ofile[fd];
    if (!f) {
        // printf("sys_read: file not found for fd=%d\n", fd);
        return -1;
    }
    
    if (!f->readable) {
        // printf("sys_read: file not readable (type=%d)\n", f->type);
        return -1;
    }
    
    // 简化实现：从控制台读取
    if (f->type == FD_DEVICE && f->major == DEV_CONSOLE) {
        return 0;  // 暂时返回0
    }
    
    // printf("sys_read: calling file_read (type=%d, offset=%d, count=%d)\n", 
    //        f->type, f->offset, count);
    
    // ✅ 对普通文件和目录添加事务支持（因为会更新访问时间，触发写操作）
    int result;
    if (f->type == FD_FILE || f->type == FD_DIR) {
        begin_op();
        result = file_read(f, count, buf_addr, true);
        end_op();
    } else {
        // 设备文件不需要事务
        result = file_read(f, count, buf_addr, true);
    }
    
    // printf("sys_read: file_read returned %d\n", result);
    return result;
}

// sys_open - 打开文件
uint64 sys_open(void)
{
    char path[256];
    int flags;
    uint64 path_ptr;
    
    // 获取参数
    if (argaddr(0, &path_ptr) < 0 || argint(1, &flags) < 0) {
        return -1;
    }
    
    // ✅ 从用户空间拷贝路径
    if (get_user_string(path_ptr, path, sizeof(path)) < 0) {
        return -1;
    }
    
    // ✅ 添加安全检查
    if (security_check_file_permission(path, flags) != 0) {
        return -1;
    }
    
    proc_t* p = myproc();
    if (!p) {
        return -1;
    }
    
    // ✅ 注意：不需要在这里调用 begin_op()，因为 file_open() -> path_create_inode() 内部已经处理了事务
    // 对于非创建模式，path_to_inode() 不需要事务，但为了统一处理，我们在 file_open 内部处理
    
    // ✅ 将用户标志转换为内部模式
    uint32 open_mode = 0;
    if (flags & O_CREATE) open_mode |= MODE_CREATE;
    
    // ✅ 修复：O_RDONLY 是 0，需要使用 O_ACCMODE 掩码提取访问模式
    uint32 access_mode = flags & O_ACCMODE;
    if (access_mode == O_RDONLY || access_mode == O_RDWR) {
        open_mode |= MODE_READ;
    }
    if (access_mode == O_WRONLY || access_mode == O_RDWR) {
        open_mode |= MODE_WRITE;
    }
    
    printf("sys_open: flags=0x%x, access_mode=0x%x, open_mode=0x%x\n", 
           flags, access_mode, open_mode);
    
    struct file* f = file_open(path, open_mode);
    
    if (!f) {
        // ✅ file_open 内部已经处理了事务，失败时不需要额外调用 end_op()
        return -1;
    }
    
    // ✅ 检查 inode 是否有效
    if (f->type == FD_FILE || f->type == FD_DIR) {
        if (!f->ip) {
            file_close(f);
            return -1;
        }

        // ✅ 关键：确保 inode 在返回前被解锁（file_open 已经解锁了）
        if (sleeplock_holding(&f->ip->slk)) {
            inode_unlock(f->ip);
        }
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
        file_close(f);
        return -1;
    }
    
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
    
    // ✅ 添加调试：检查文件类型和 inode 状态
    printf("sys_close: file type=%d, ref=%d\n", f->type, f->ref);
    if (f->type == FD_FILE || f->type == FD_DIR) {
        if (f->ip) {
            printf("sys_close: inode num=%d, ref=%d, locked=%d\n",
                   f->ip->inode_num, f->ip->ref, sleeplock_holding(&f->ip->slk));
        }
    }
    
    // ✅ 先清除文件描述符
    p->ofile[fd] = NULL;
    
    // ✅ 然后关闭文件
    printf("sys_close: calling file_close...\n");
    file_close(f);
    
    printf("sys_close: closed fd %d successfully\n", fd);
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
uint64 sys_fork(void) {
    proc_t *parent = myproc();
    proc_t *child;
    
    printf("sys_fork: parent PID=%d creating child\n", parent->pid);
    
    // 分配新进程
    child = proc_alloc();
    if (!child) {
        printf("sys_fork: failed to allocate child process\n");
        return -1;
    }
    
    printf("sys_fork: allocated child PID=%d\n", child->pid);
    
    // ✅ 关键：正确设置父子关系
    child->parent = parent;
    printf("sys_fork: set child->parent = %p (PID=%d)\n", parent, parent->pid);
    
    // 复制父进程的内存
    if (proc_copy_memory(parent, child) < 0) {
        printf("sys_fork: failed to copy memory\n");
        proc_free(child);
        return -1;
    }
    
    // 复制父进程的文件描述符
    proc_copy_files(parent, child);
    
    // ✅ 复制trapframe
    if (!child->tf || !parent->tf) {
        printf("sys_fork: invalid trapframe\n");
        proc_free(child);
        return -1;
    }
    
    // ✅ 先保存子进程的 kernel_sp 和其他内核字段
    extern pagetable_t kernel_pagetable;
    uint64 child_kernel_sp = child->kstack + PGSIZE;
    uint64 child_kernel_satp = MAKE_SATP(kernel_pagetable);
    uint64 child_kernel_trap = (uint64)trap_user_handler;
    
    // 复制父进程的trapframe到子进程
    *child->tf = *parent->tf;
    
    // ✅ 恢复子进程自己的内核字段（关键！）
    child->tf->kernel_sp = child_kernel_sp;
    child->tf->kernel_satp = child_kernel_satp;
    child->tf->kernel_trap = child_kernel_trap;
    
    // ✅ 设置子进程的返回值为0
    child->tf->a0 = 0;
    
    // 设置子进程状态为可运行
    child->state = PROC_RUNNABLE;
    
    printf("sys_fork: child PID=%d created successfully\n", child->pid);
    printf("sys_fork: child->parent = 0x%lx (PID=%d)\n", (uint64)child->parent, child->parent->pid);
    printf("sys_fork: verification - parent=0x%lx, child->parent=0x%lx\n", 
           (uint64)parent, (uint64)child->parent);
    
    // 父进程返回子进程PID
    return child->pid;
}

// sys_exit - 退出进程
uint64 sys_exit(void) {
    proc_t *p = myproc();
    int status;
    
    if (argint(0, &status) < 0) {
        return -1;
    }
    
    // 关闭所有打开的文件
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            file_close(p->ofile[fd]);
            p->ofile[fd] = 0;
        }
    }
    
    // 释放当前目录
    if (p->cwd) {
        inode_free(p->cwd);
        p->cwd = 0;
    }
    
    // 保存退出状态
    p->exit_code = status;
    
    // 将所有子进程重新分配给init进程
    for (proc_t *child = proc_table; child < &proc_table[MAX_PROC]; child++) {
        if (child->parent == p) {
            child->parent = initproc;
            wakeup(initproc);  // 唤醒init进程收集僵尸进程
        }
    }
    
    // 唤醒父进程（如果在wait）
    if (p->parent) {
        wakeup(p->parent);
    }
    
    // 获取进程锁并设置为僵尸状态
    spinlock_acquire(&p->lock);
    p->state = PROC_ZOMBIE;
    
    // 调度其他进程，永不返回
    sched();
    
    // ✅ 使用 __builtin_unreachable() 告诉编译器这里永不到达
    __builtin_unreachable();
}

// sys_wait - 等待子进程
uint64 sys_wait(void) {
    proc_t *p = myproc();
    proc_t *child;
    int havekids, pid;
    uint64 addr;
    
    if (argaddr(0, &addr) < 0) {
        return -1;
    }
    
    printf("sys_wait: ENTRY - process %s (PID=%d) waiting for children\n", p->name, p->pid);
    
    spinlock_acquire(&wait_lock);
    
    for (;;) {
        printf("sys_wait: LOOP START - scanning for children\n");
        
        havekids = 0;
        for (child = proc_table; child < &proc_table[MAX_PROC]; child++) {
            if (child->parent == p) {
                spinlock_acquire(&child->lock);
                
                havekids = 1;
                if (child->state == PROC_ZOMBIE) {
                    // 找到僵尸子进程
                    pid = child->pid;
                    int exit_code = child->exit_code;  // ✅ 先保存退出码
                    
                    printf("sys_wait: found zombie child PID=%d, exit_status=%d\n", 
                           pid, exit_code);
                    
                    // ✅ 复制退出状态到用户空间
                    if (addr != 0 && copyout(p->pgtbl, addr, 
                                            (char*)&exit_code, sizeof(int)) < 0) {
                        spinlock_release(&child->lock);
                        spinlock_release(&wait_lock);
                        return -1;
                    }
                    
                    // ✅ 使用 proc_free() 来清理子进程
                    // proc_free() 会自动释放内存、文件描述符等
                    spinlock_release(&child->lock);  // 先释放锁
                    proc_free(child);  // 然后清理
                    
                    spinlock_release(&wait_lock);
                    
                    printf("sys_wait: SUCCESS - returning child PID=%d\n", pid);
                    return pid;
                }
                spinlock_release(&child->lock);
            }
        }
        
        if (!havekids || p->killed) {
            printf("sys_wait: NO CHILDREN - returning -1\n");
            spinlock_release(&wait_lock);
            return -1;
        }
        
        printf("sys_wait: SLEEPING - waiting for child to exit (channel=0x%lx)\n", (uint64)p);
        sleep(p, &wait_lock);
        
        printf("sys_wait: WOKE UP - continuing\n");
    }
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

// 系统调用实现
uint64 sys_yield(void) {
    proc_t *p = myproc();
    spinlock_acquire(&p->lock);
    p->state = PROC_RUNNABLE;
    sched();
    spinlock_release(&p->lock);
    return 0;
}

// sys_pipe - 创建管道
uint64 sys_pipe(void) {
    uint64 fdarray;  // 用户空间的 int[2] 数组地址
    struct file *rf, *wf;
    int fd0, fd1;
    proc_t *p = myproc();
    
    printf("sys_pipe: creating pipe\n");
    
    // 获取参数：用户空间数组地址
    if (argaddr(0, &fdarray) < 0) {
        printf("sys_pipe: failed to get argument\n");
        return -1;
    }
    
    printf("sys_pipe: user array address = 0x%lx\n", fdarray);
    
    // 分配管道
    if (pipealloc(&rf, &wf) < 0) {
        printf("sys_pipe: pipealloc failed\n");
        return -1;
    }
    
    // 分配文件描述符 0（读端）
    fd0 = -1;
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i] == NULL) {
            fd0 = i;
            p->ofile[fd0] = rf;
            break;
        }
    }
    
    if (fd0 < 0) {
        printf("sys_pipe: no available fd for read end\n");
        goto bad;
    }
    
    // 分配文件描述符 1（写端）
    fd1 = -1;
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i] == NULL) {
            fd1 = i;
            p->ofile[fd1] = wf;
            break;
        }
    }
    
    if (fd1 < 0) {
        printf("sys_pipe: no available fd for write end\n");
        goto bad;
    }
    
    printf("sys_pipe: allocated fd0=%d (read), fd1=%d (write)\n", fd0, fd1);
    
    // 将 fd0 写到 fdarray[0]
    uvm_copyout(p->pgtbl, fdarray, (uint64)&fd0, sizeof(fd0));
    
    // 将 fd1 写到 fdarray[1]
    uvm_copyout(p->pgtbl, fdarray + sizeof(int), (uint64)&fd1, sizeof(fd1));
    
    printf("sys_pipe: success, returned fd[0]=%d, fd[1]=%d\n", fd0, fd1);
    return 0;

bad:
    if (fd0 >= 0)
        p->ofile[fd0] = NULL;
    if (fd1 >= 0)
        p->ofile[fd1] = NULL;
    file_close(rf);
    file_close(wf);
    return -1;
}

// sys_lseek - 移动文件指针
uint64 sys_lseek(void)
{
    int fd, whence;
    int offset;  // RISC-V 使用 int 类型
    proc_t *p = myproc();
    
    if (argint(0, &fd) < 0 || argint(1, &offset) < 0 || argint(2, &whence) < 0) {
        return -1;
    }
    
    printf("sys_lseek: fd=%d, offset=%d, whence=%d\n", fd, offset, whence);
    
    if (fd < 0 || fd >= NOFILE) {
        return -1;
    }
    
    struct file *f = p->ofile[fd];
    if (!f) {
        return -1;
    }
    
    // 调用文件系统层
    return file_lseek(f, offset, whence);
}

// sys_stat - 获取文件信息
uint64 sys_stat(void)
{
    char path[256];
    uint64 statbuf_addr;
    uint64 path_addr;
    proc_t *p = myproc();
    
    if (argaddr(0, &path_addr) < 0 || argaddr(1, &statbuf_addr) < 0) {
        return -1;
    }
    
    if (get_user_string(path_addr, path, sizeof(path)) < 0) {
        return -1;
    }
    
    printf("sys_stat: path='%s'\n", path);
    
    // 获取 inode
    inode_t *ip = path_to_inode(path);
    if (!ip) {
        return SYSCALL_ENOENT;
    }
    
    inode_lock(ip);
    
    // 填充 stat 结构
    struct stat st;
    st.dev = 0;              // 设备号（简化）
    st.ino = ip->inode_num;  // inode 号
    st.type = ip->type;      // 文件类型
    st.nlink = ip->nlink;    // 硬链接数
    st.size = ip->size;      // 文件大小
    
    inode_unlock_free(ip);
    
    // 复制到用户空间
    if (copyout(p->pgtbl, statbuf_addr, (char*)&st, sizeof(st)) < 0) {
        return -1;
    }
    
    return 0;
}

// sys_fstat - 获取打开文件信息
uint64 sys_fstat(void)
{
    int fd;
    uint64 statbuf_addr;
    proc_t *p = myproc();
    struct file *f;
    
    if (argint(0, &fd) < 0 || argaddr(1, &statbuf_addr) < 0) {
        return -1;
    }
    
    printf("sys_fstat: fd=%d\n", fd);
    
    if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == NULL) {
        return -1;
    }
    
    // 调用文件系统层
    return file_stat(f, statbuf_addr);
}

// sys_mkdir - 创建目录
uint64 sys_mkdir(void)
{
    char path[256];
    uint64 path_addr;
    int mode;
    
    if (argaddr(0, &path_addr) < 0 || argint(1, &mode) < 0) {
        return -1;
    }
    
    if (get_user_string(path_addr, path, sizeof(path)) < 0) {
        return -1;
    }
    
    printf("sys_mkdir: path='%s', mode=%d\n", path, mode);
    
    // ✅ 开始事务
    begin_op();
    
    // 调用文件系统层创建目录
    inode_t *ip = path_create_inode(path, FT_DIR, 0, 0);
    
    if (!ip) {
        end_op();  // ✅ 失败时结束事务
        printf("sys_mkdir: failed to create directory\n");
        return SYSCALL_EEXIST;  // 或其他错误
    }
    
    // ✅ 成功后释放 inode 并结束事务
    inode_free(ip);
    end_op();
    
    printf("sys_mkdir: directory created successfully\n");
    return 0;
}

// sys_chdir - 切换当前目录
uint64 sys_chdir(void)
{
    char path[256];
    uint64 path_addr;
    
    if (argaddr(0, &path_addr) < 0) {
        return -1;
    }
    
    if (get_user_string(path_addr, path, sizeof(path)) < 0) {
        return -1;
    }
    
    printf("sys_chdir: path='%s'\n", path);
    
    // 调用文件系统层
    if (dir_change(path) < 0) {
        return SYSCALL_ENOENT;
    }
    
    return 0;
}

// sys_getcwd - 获取当前目录
uint64 sys_getcwd(void)
{
    uint64 buf_addr;
    int size;
    proc_t *p = myproc();
    
    if (argaddr(0, &buf_addr) < 0 || argint(1, &size) < 0) {
        return -1;
    }
    
    printf("sys_getcwd: buf=0x%lx, size=%d\n", buf_addr, size);
    
    if (size < 1) {
        printf("sys_getcwd: invalid size\n");
        return -1;
    }
    
    // 使用 inode_to_path 构建完整路径
    // ✅ 注意：不要在这里锁定 inode，因为 inode_to_path_recursive 会自己管理锁
    char cwd[DIR_PATH_LEN];
    if (inode_to_path(p->cwd, cwd, sizeof(cwd)) == NULL) {
        printf("sys_getcwd: inode_to_path failed\n");
        return -1;
    }
    
    // 检查用户缓冲区大小
    int len = strlen(cwd);
    if (len + 1 > size) {
        printf("sys_getcwd: buffer too small (need %d, got %d)\n", len + 1, size);
        return -1;
    }
    
    // 复制到用户空间
    if (copyout(p->pgtbl, buf_addr, cwd, len + 1) < 0) {
        printf("sys_getcwd: copyout failed\n");
        return -1;
    }
    
    printf("sys_getcwd: success, path='%s'\n", cwd);
    return buf_addr;
}


// sys_link - 创建硬链接
uint64 sys_link(void)
{
    char oldpath[256], newpath[256];
    uint64 oldpath_addr, newpath_addr;
    
    if (argaddr(0, &oldpath_addr) < 0 || argaddr(1, &newpath_addr) < 0) {
        return -1;
    }
    
    if (get_user_string(oldpath_addr, oldpath, sizeof(oldpath)) < 0) {
        return -1;
    }
    
    if (get_user_string(newpath_addr, newpath, sizeof(newpath)) < 0) {
        return -1;
    }
    
    printf("sys_link: oldpath='%s', newpath='%s'\n", oldpath, newpath);
    
    // 调用文件系统层
    if (path_link(oldpath, newpath) < 0) {
        return -1;
    }
    
    return 0;
}

// sys_unlink - 删除文件/链接
uint64 sys_unlink(void)
{
    char path[256];
    uint64 path_addr;
    
    if (argaddr(0, &path_addr) < 0) {
        return -1;
    }
    
    if (get_user_string(path_addr, path, sizeof(path)) < 0) {
        return -1;
    }
    
    // printf("sys_unlink: path='%s'\n", path);
    
    // ✅ 开始事务
    begin_op();
    
    // 调用文件系统层删除文件
    int ret = path_unlink(path);
    
    if (ret < 0) {
        end_op();  // ✅ 失败时结束事务
        printf("sys_unlink: failed to unlink\n");
        return SYSCALL_ENOENT;
    }
    
    // ✅ 成功后结束事务
    end_op();
    
    printf("sys_unlink: unlinked successfully\n");
    return 0;
}

// sys_dup - 复制文件描述符
uint64 sys_dup(void)
{
    int oldfd;
    proc_t *p = myproc();
    
    if (argint(0, &oldfd) < 0) {
        return -1;
    }
    
    printf("sys_dup: oldfd=%d\n", oldfd);
    
    if (oldfd < 0 || oldfd >= NOFILE) {
        return -1;
    }
    
    struct file *f = p->ofile[oldfd];
    if (!f) {
        return -1;
    }
    
    // 查找空闲的文件描述符
    int newfd = -1;
    for (int i = 0; i < NOFILE; i++) {
        if (p->ofile[i] == NULL) {
            newfd = i;
            break;
        }
    }
    
    if (newfd < 0) {
        return SYSCALL_EMFILE;  // 文件描述符用尽
    }
    
    // 增加引用计数
    file_dup(f);
    p->ofile[newfd] = f;
    
    printf("sys_dup: duplicated fd %d to fd %d\n", oldfd, newfd);
    return newfd;
}

// sys_dup2 - 复制文件描述符到指定位置
uint64 sys_dup2(void)
{
    int oldfd, newfd;
    proc_t *p = myproc();
    
    if (argint(0, &oldfd) < 0 || argint(1, &newfd) < 0) {
        return -1;
    }
    
    printf("sys_dup2: oldfd=%d, newfd=%d\n", oldfd, newfd);
    
    if (oldfd < 0 || oldfd >= NOFILE || newfd < 0 || newfd >= NOFILE) {
        return -1;
    }
    
    struct file *f = p->ofile[oldfd];
    if (!f) {
        return -1;
    }
    
    // 如果 oldfd == newfd，直接返回
    if (oldfd == newfd) {
        return newfd;
    }
    
    // 如果 newfd 已打开，先关闭
    if (p->ofile[newfd]) {
        file_close(p->ofile[newfd]);
    }
    
    // 复制文件描述符
    file_dup(f);
    p->ofile[newfd] = f;
    
    printf("sys_dup2: duplicated fd %d to fd %d\n", oldfd, newfd);
    return newfd;
}

// ============================================
// sys_symlink - 创建符号链接
// ============================================
uint64 sys_symlink(void)
{
    char target[DIR_PATH_LEN], linkpath[DIR_PATH_LEN];
    uint64 target_addr, linkpath_addr;
    
    if (argaddr(0, &target_addr) < 0 || argaddr(1, &linkpath_addr) < 0) {
        printf("sys_symlink: failed to get arguments\n");
        return -1;
    }
    
    // 从用户空间复制字符串
    if (get_user_string(target_addr, target, sizeof(target)) < 0) {
        printf("sys_symlink: failed to copy target string\n");
        return -1;
    }
    
    if (get_user_string(linkpath_addr, linkpath, sizeof(linkpath)) < 0) {
        printf("sys_symlink: failed to copy linkpath string\n");
        return -1;
    }
    
    printf("sys_symlink: target='%s', linkpath='%s'\n", target, linkpath);
    
    // 参数验证
    if (strlen(target) == 0) {
        printf("sys_symlink: empty target path\n");
        return SYSCALL_EINVAL;
    }
    
    if (strlen(linkpath) == 0) {
        printf("sys_symlink: empty linkpath\n");
        return SYSCALL_EINVAL;
    }
    
    // ✅ 调用 path_create_symlink 创建符号链接
    inode_t* ip = path_create_symlink(target, linkpath);
    if (!ip) {
        printf("sys_symlink: path_create_symlink failed\n");
        
        // 检查链接是否已存在
        inode_t* existing = path_to_inode(linkpath);
        if (existing) {
            inode_free(existing);
            return SYSCALL_EEXIST;  // 文件已存在
        }
        
        return SYSCALL_EIO;  // 一般性错误
    }
    
    inode_free(ip);
    printf("sys_symlink: successfully created symlink\n");
    return 0;
}

// ============================================
// sys_readlink - 读取符号链接的目标路径
// ============================================
uint64 sys_readlink(void)
{
    char path[DIR_PATH_LEN];
    uint64 path_addr, buf_addr;
    int size;
    proc_t *p = myproc();
    
    if (argaddr(0, &path_addr) < 0 || 
        argaddr(1, &buf_addr) < 0 || 
        argint(2, &size) < 0) {
        printf("sys_readlink: failed to get arguments\n");
        return -1;
    }
    
    // 从用户空间复制路径字符串
    if (get_user_string(path_addr, path, sizeof(path)) < 0) {
        printf("sys_readlink: failed to copy path string\n");
        return -1;
    }
    
    printf("sys_readlink: path='%s', size=%d\n", path, size);
    
    // 参数验证
    if (size <= 0) {
        printf("sys_readlink: invalid size\n");
        return -1;
    }
    
    // ✅ 获取符号链接的 inode（不自动解析）
    // 注意：需要使用不解析符号链接的版本
    char name[DIR_NAME_LEN];
    inode_t* dp = path_to_pinode(path, name);
    if (!dp) {
        printf("sys_readlink: parent directory not found\n");
        return SYSCALL_ENOENT;
    }
    
    inode_lock(dp);
    uint16 inum = dir_search_entry(dp, name);
    inode_unlock_free(dp);
    
    if (inum == INODE_NUM_UNUSED) {
        printf("sys_readlink: file not found\n");
        return SYSCALL_ENOENT;
    }
    
    inode_t* ip = inode_alloc(inum);
    inode_lock(ip);
    
    // ✅ 检查是否是符号链接
    if (ip->type != FT_SYMLINK) {
        printf("sys_readlink: not a symbolic link (type=%d)\n", ip->type);
        inode_unlock_free(ip);
        return SYSCALL_EINVAL;
    }
    
    // ✅ 读取符号链接的目标路径
    char target[DIR_PATH_LEN];
    uint32 len = ip->size;
    
    if (len >= sizeof(target)) {
        len = sizeof(target) - 1;
    }
    
    uint32 read_len = inode_read_data(ip, 0, len, target, false);
    if (read_len != len) {
        printf("sys_readlink: failed to read symlink data\n");
        inode_unlock_free(ip);
        return SYSCALL_EIO;
    }
    target[len] = '\0';
    
    inode_unlock_free(ip);
    
    // ✅ 限制返回长度（符合 POSIX 规范）
    int copy_len = len;
    if (copy_len > size) {
        copy_len = size;
    }
    
    // ✅ 复制到用户空间（注意：不包含 '\0'，符合 readlink 规范）
    if (copyout(p->pgtbl, buf_addr, target, copy_len) < 0) {
        printf("sys_readlink: copyout failed\n");
        return SYSCALL_EFAULT;
    }
    
    printf("sys_readlink: success, target='%s' (returned %d bytes)\n", target, copy_len);
    
    // ✅ 返回实际复制的字节数
    return copy_len;
}
