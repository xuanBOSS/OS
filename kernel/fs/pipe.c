#include "common.h"
#include "fs/file.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "mem/str.h"
#include "mem/pmem.h"      // ✅ 正确的头文件
#include "proc/proc.h"
#include "mem/vmem.h"

#define PIPESIZE 512

// 分配管道及其两个文件描述符
int pipealloc(struct file **f0, struct file **f1)
{
    struct pipe *pi;
    
    printf("pipealloc: allocating pipe\n");
    
    pi = NULL;
    *f0 = *f1 = NULL;
    
    // ✅ 修复：使用 pmem_alloc 代替 kalloc
    if ((pi = (struct pipe*)pmem_alloc(true)) == NULL) {
        printf("pipealloc: failed to allocate pipe structure\n");
        goto bad;
    }
    
    // 初始化管道
    pi->readopen = 1;
    pi->writeopen = 1;
    pi->nwrite = 0;
    pi->nread = 0;
    spinlock_init(&pi->lock, "pipe");
    memset(pi->data, 0, PIPESIZE);
    
    // 分配两个文件结构
    if ((*f0 = filealloc()) == NULL || (*f1 = filealloc()) == NULL) {
        printf("pipealloc: failed to allocate file structures\n");
        goto bad;
    }
    
    // 设置读端
    (*f0)->type = FD_PIPE_E;
    (*f0)->readable = 1;
    (*f0)->writable = 0;
    (*f0)->pipe = pi;
    
    // 设置写端
    (*f1)->type = FD_PIPE_E;
    (*f1)->readable = 0;
    (*f1)->writable = 1;
    (*f1)->pipe = pi;
    
    printf("pipealloc: pipe allocated successfully\n");
    return 0;

bad:
    if (pi)
        pmem_free((uint64)pi, true);  // ✅ 修复：使用 pmem_free
    if (*f0)
        fileclose(*f0);
    if (*f1)
        fileclose(*f1);
    return -1;
}

// 关闭管道
void pipeclose(struct pipe *pi, int writable)
{
    if (!pi) {
        printf("pipeclose: null pipe pointer\n");
        return;
    }
    
    spinlock_acquire(&pi->lock);
    
    if (writable) {
        pi->writeopen = 0;
        printf("pipeclose: write end closed\n");
        wakeup(&pi->nread);
    } else {
        pi->readopen = 0;
        printf("pipeclose: read end closed\n");
        wakeup(&pi->nwrite);
    }
    
    // 如果两端都关闭，释放管道
    if (pi->readopen == 0 && pi->writeopen == 0) {
        spinlock_release(&pi->lock);
        pmem_free((uint64)pi, true);  // ✅ 修复：使用 pmem_free
        printf("pipeclose: pipe freed\n");
    } else {
        spinlock_release(&pi->lock);
    }
}

// 写入管道
int pipewrite(struct pipe *pi, uint64 addr, int n)
{
    int i = 0;
    proc_t *pr = myproc();
    
    if (!pi) {
        printf("pipewrite: null pipe pointer\n");
        return -1;
    }
    
    printf("pipewrite: writing %d bytes\n", n);
    
    spinlock_acquire(&pi->lock);
    
    while (i < n) {
        if (pi->readopen == 0) {
            spinlock_release(&pi->lock);
            printf("pipewrite: read end closed, returning -1\n");
            return -1;
        }
        
        if (pi->nwrite == pi->nread + PIPESIZE) {
            printf("pipewrite: pipe full, sleeping\n");
            wakeup(&pi->nread);
            sleep(&pi->nwrite, &pi->lock);
            continue;
        }
        
        // 从用户空间复制一个字节到内核
        char ch;
        uvm_copyin(pr->pgtbl, (uint64)&ch, addr + i, 1);
        
        pi->data[pi->nwrite++ % PIPESIZE] = ch;
        i++;
    }
    
    wakeup(&pi->nread);
    spinlock_release(&pi->lock);
    
    printf("pipewrite: wrote %d bytes\n", i);
    return i;
}

// 从管道读取
int piperead(struct pipe *pi, uint64 addr, int n)
{
    int i;
    proc_t *pr = myproc();
    char ch;
    
    if (!pi) {
        printf("piperead: null pipe pointer\n");
        return -1;
    }
    
    printf("piperead: reading up to %d bytes\n", n);
    
    spinlock_acquire(&pi->lock);
    
    // 等待数据或写端关闭
    while (pi->nread == pi->nwrite && pi->writeopen) {
        printf("piperead: no data, sleeping\n");
        sleep(&pi->nread, &pi->lock);
    }
    
    // 读取数据
    for (i = 0; i < n; i++) {
        if (pi->nread == pi->nwrite) {
            break;
        }
        
        ch = pi->data[pi->nread++ % PIPESIZE];
        
        // 从内核复制一个字节到用户空间
        uvm_copyout(pr->pgtbl, addr + i, (uint64)&ch, 1);
    }
    
    wakeup(&pi->nwrite);
    spinlock_release(&pi->lock);
    
    printf("piperead: read %d bytes\n", i);
    return i;
}
