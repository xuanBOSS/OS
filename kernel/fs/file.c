#include "common.h"
#include "fs/file.h"
#include "fs/stat.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "mem/str.h"
#include "proc/proc.h" 
#include "dev/uart.h"
#include "mem/vmem.h"   

// 全局文件表
struct file ftable[NFILE];
spinlock_t ftable_lock;

// 简化的 inode 表
struct inode inode_table[100];
spinlock_t inode_lock;

// 设备表
struct {
    int (*read)(int, uint64, int);
    int (*write)(int, uint64, int);
} devsw[NDEV];

// 初始化文件系统
void fileinit(void)
{
    printf("=== File System Initialization ===\n");
    
    spinlock_init(&ftable_lock, "ftable");
    spinlock_init(&inode_lock, "inode");
    
    // 初始化文件表
    for (int i = 0; i < NFILE; i++) {
        ftable[i].type = FD_NONE_E;
        ftable[i].ref = 0;
    }
    
    // 初始化 inode 表
    for (int i = 0; i < 100; i++) {
        inode_table[i].ref = 0;
        inode_table[i].valid = 0;
        initsleeplock(&inode_table[i].lock, "inode");
    }
    
    // 初始化设备表
    for (int i = 0; i < NDEV; i++) {
        devsw[i].read = NULL;
        devsw[i].write = NULL;
    }
    
    // 注册控制台设备
    devsw[CONSOLE].read = console_read;
    devsw[CONSOLE].write = console_write;
    
    printf("File system initialized successfully!\n");
    printf("- File table: %d entries\n", NFILE);
    printf("- Inode table: 100 entries\n");
    printf("- Device table: %d entries\n", NDEV);
    printf("- Console device registered at slot %d\n", CONSOLE);
    printf("=====================================\n");
}

// 分配文件结构
struct file* filealloc(void)
{
    struct file *f;
    
    spinlock_acquire(&ftable_lock);
    for (f = ftable; f < ftable + NFILE; f++) {
        if (f->ref == 0) {
            f->ref = 1;
            f->type = FD_NONE_E;
            f->readable = 0;
            f->writable = 0;
            f->pipe = NULL;
            f->ip = NULL;
            f->off = 0;
            f->major = 0;
            spinlock_release(&ftable_lock);
            printf("filealloc: allocated file structure at index %ld\n", f - ftable);
            return f;
        }
    }
    spinlock_release(&ftable_lock);
    printf("filealloc: no available file structures\n");
    return NULL;
}

// 复制文件引用
struct file* filedup(struct file *f)
{
    if (!f) {
        printf("filedup: null file pointer\n");
        return NULL;
    }
    
    spinlock_acquire(&ftable_lock);
    if (f->ref < 1) {
        spinlock_release(&ftable_lock);
        printf("filedup: invalid reference count %d\n", f->ref);
        return NULL;
    }
    f->ref++;
    spinlock_release(&ftable_lock);
    
    printf("filedup: duplicated file, new ref count = %d\n", f->ref);
    return f;
}

// 关闭文件
void fileclose(struct file *f)
{
    struct file ff;
    
    if (!f) {
        printf("fileclose: null file pointer\n");
        return;
    }
    
    spinlock_acquire(&ftable_lock);
    if (f->ref < 1) {
        spinlock_release(&ftable_lock);
        printf("fileclose: invalid reference count %d\n", f->ref);
        return;
    }
    
    if (--f->ref > 0) {
        spinlock_release(&ftable_lock);
        printf("fileclose: decremented ref count to %d\n", f->ref);
        return;
    }
    
    // 保存文件信息并清理
    ff = *f;
    f->ref = 0;
    f->type = FD_NONE_E;
    f->readable = 0;
    f->writable = 0;
    f->pipe = NULL;
    f->ip = NULL;
    f->off = 0;
    f->major = 0;
    spinlock_release(&ftable_lock);
    
    printf("fileclose: closing file type %d\n", ff.type);
    
    if (ff.type == FD_PIPE_E) {
        pipeclose(ff.pipe, ff.writable);
    } else if (ff.type == FD_INODE_E || ff.type == FD_DEVICE_E) {
        if (ff.ip) {
            iput(ff.ip);
        }
        printf("fileclose: inode closed\n");
    }
}

// 读文件
int fileread(struct file *f, uint64 addr, int n)
{
    int r = 0;
    
    if (!f) {
        printf("fileread: null file pointer\n");
        return -1;
    }
    
    if (f->readable == 0) {
        printf("fileread: file not readable\n");
        return -1;
    }
    
    printf("fileread: reading %d bytes from file type %d\n", n, f->type);
    
    if (f->type == FD_PIPE_E) {
        r = piperead(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE_E) {
        // ... 设备代码保持不变
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].read) {
            printf("fileread: invalid device %d\n", f->major);
            return -1;
        }
        printf("fileread: reading from device %d\n", f->major);
        r = devsw[f->major].read(1, addr, n);
    } else if (f->type == FD_INODE_E) {
        // ... inode 代码保持不变
        if (!f->ip) {
            printf("fileread: null inode pointer\n");
            return -1;
        }
        
        ilock(f->ip);
        
        // 简化实现：模拟从文件读取
        printf("fileread: reading from inode %d at offset %d\n", f->ip->inum, f->off);
        
        // 模拟读取一些数据
        static char simulated_data[] = "Hello from file system!\nThis is a test file.\nEnd of file.\n";
        uint32 data_len = sizeof(simulated_data) - 1;  // 不包括 null terminator
        
        if (f->off >= data_len) {
            r = 0;  // EOF
            printf("fileread: EOF reached\n");
        } else {
            uint32 available = data_len - f->off;
            if (n > available) n = available;
            
            // 在实际实现中需要使用 copyout
            printf("fileread: would copy %d bytes to user address 0x%lx\n", n, addr);
            printf("fileread: data: \"");
            for (int i = 0; i < n && i < 50; i++) {  // 最多显示50个字符
                char c = simulated_data[f->off + i];
                if (c >= 32 && c <= 126) {
                    printf("%c", c);
                } else if (c == '\n') {
                    printf("\\n");
                } else {
                    printf("\\x%02x", c);
                }
            }
            if (n > 50) printf("...");
            printf("\"\n");
            
            f->off += n;
            r = n;
        }
        
        iunlock(f->ip);
    } else {
        printf("fileread: unknown file type %d\n", f->type);
        r = -1;
    }
    
    printf("fileread: returning %d bytes\n", r);
    return r;
}

// 写文件
int filewrite(struct file *f, uint64 addr, int n)
{
    int ret = 0;
    
    if (!f) {
        printf("filewrite: null file pointer\n");
        return -1;
    }
    
    if (f->writable == 0) {
        printf("filewrite: file not writable\n");
        return -1;
    }
    
    printf("filewrite: writing %d bytes to file type %d\n", n, f->type);
    
    if (f->type == FD_PIPE_E) {
        ret = pipewrite(f->pipe, addr, n);
    } else if (f->type == FD_DEVICE_E) {
        // ... 设备代码保持不变
        if (f->major < 0 || f->major >= NDEV || !devsw[f->major].write) {
            printf("filewrite: invalid device %d\n", f->major);
            return -1;
        }
        printf("filewrite: writing to device %d\n", f->major);
        ret = devsw[f->major].write(1, addr, n);
    } else if (f->type == FD_INODE_E) {
        if (!f->ip) {
            printf("filewrite: null inode pointer\n");
            return -1;
        }
        
        ilock(f->ip);
        
        // 简化实现：模拟写入文件
        printf("filewrite: writing to inode %d at offset %d\n", f->ip->inum, f->off);
        
        // 在实际实现中需要：
        // 1. 使用 copyin 从用户空间复制数据
        // 2. 分配磁盘块
        // 3. 写入数据到磁盘
        // 4. 更新 inode
        
        printf("filewrite: would copy %d bytes from user address 0x%lx\n", n, addr);
        printf("filewrite: data written to file at offset %d\n", f->off);
        
        // 更新文件大小和偏移
        if (f->off + n > f->ip->size) {
            f->ip->size = f->off + n;
            printf("filewrite: updated file size to %d\n", f->ip->size);
        }
        
        f->off += n;
        ret = n;
        
        iunlock(f->ip);
    } else {
        printf("filewrite: unknown file type %d\n", f->type);
        ret = -1;
    }
    
    printf("filewrite: returning %d bytes\n", ret);
    return ret;
}

// 获取文件状态
int filestat(struct file *f, uint64 st)
{
    if (!f) {
        printf("filestat: null file pointer\n");
        return -1;
    }
    
    if (f->type == FD_INODE_E || f->type == FD_DEVICE_E) {
        if (!f->ip) {
            printf("filestat: null inode pointer\n");
            return -1;
        }
        
        ilock(f->ip);
        
        // 简化实现：返回模拟的文件状态
        struct stat stat_buf;
        stat_buf.dev = f->ip->dev;
        stat_buf.ino = f->ip->inum;
        stat_buf.type = f->ip->type;
        stat_buf.nlink = f->ip->nlink;
        stat_buf.size = f->ip->size;
        
        printf("filestat: returning file stats\n");
        printf("  dev=%d, ino=%d, type=%d, nlink=%d, size=%lld\n",
               stat_buf.dev, stat_buf.ino, stat_buf.type, stat_buf.nlink, stat_buf.size);
        
        // 在实际实现中需要使用 copyout
        printf("filestat: would copy stat to user address 0x%lx\n", st);
        
        iunlock(f->ip);
        return 0;
    }
    
    printf("filestat: unsupported file type %d\n", f->type);
    return -1;
}

// 控制台读取（设备驱动）
int console_read(int user_dst, uint64 dst, int n)
{
    printf("console_read: reading %d bytes from console\n", n);
    
    // 简化实现：模拟从控制台读取
    static char input[] = "user input from console\nline 2\nline 3\n";
    static int input_pos = 0;
    
    int len = sizeof(input) - 1 - input_pos;  // 剩余可读字节数
    if (len <= 0) {
        printf("console_read: no more input available\n");
        return 0;  // EOF
    }
    
    if (n > len) n = len;
    
    printf("console_read: would copy %d bytes to address 0x%lx\n", n, dst);
    printf("console_read: input: \"");
    for (int i = 0; i < n; i++) {
        char c = input[input_pos + i];
        if (c >= 32 && c <= 126) {
            printf("%c", c);
        } else if (c == '\n') {
            printf("\\n");
        } else {
            printf("\\x%02x", c);
        }
    }
    printf("\"\n");
    
    input_pos += n;
    return n;
}

// 控制台写入（设备驱动）
// 控制台写入（设备驱动）- 不检查uvm_copyin返回值版本
int console_write(int user_src, uint64 src, int n)
{
    printf("console_write: writing %d bytes to console\n", n);
    
    if (n <= 0) {
        printf("console_write: invalid size %d\n", n);
        return 0;
    }
    
    if (n > 256) {
        printf("console_write: limiting size from %d to 256\n", n);
        n = 256;  // 限制大小防止溢出
    }
    
    char buf[256];
    memset(buf, 0, sizeof(buf));  // 清零缓冲区
    
    proc_t *p = myproc();
    if (!p) {
        printf("console_write: no current process\n");
        return -1;
    }
    
    // 预先检查页面是否映射（可选的安全检查）
    uint64 pa = va_to_pa(p->pgtbl, src & ~(PGSIZE - 1));
    if (pa == 0) {
        printf("console_write: user page not mapped at 0x%lx\n", src);
        return -1;
    }
    
    printf("console_write: would copy %d bytes from address 0x%lx\n", n, src);
    
    // 从用户空间复制数据（不检查返回值，因为是void）
    uvm_copyin(p->pgtbl, (uint64)buf, src, n);
    
    // ✅ 实际输出用户数据到控制台
    printf("USER OUTPUT: ");
    for (int i = 0; i < n; i++) {
        uart_putc_sync(buf[i]);  // 直接输出每个字符
    }
    
    printf("console_write: [CONSOLE OUTPUT] User wrote %d bytes to console\n", n);
    
    return n;
}

// 简化的 inode 操作函数
struct inode* namei(char *path)
{
    if (!path) {
        printf("namei: null path\n");
        return NULL;
    }
    
    printf("namei: looking up path '%s'\n", path);
    
    if (strlen(path) == 0) {
        printf("namei: empty path\n");
        return NULL;
    }
    
    // 简化实现：返回模拟的 inode
    static int next_inum = 1;
    struct inode *ip = &inode_table[0];  // 使用第一个 inode
    
    if (ip->ref == 0) {
        // 初始化 inode
        ip->dev = 1;
        ip->inum = next_inum++;
        ip->valid = 1;
        ip->type = T_FILE;
        ip->major = 0;
        ip->minor = 0;
        ip->nlink = 1;
        ip->size = 1024;  // 默认文件大小
    }
    
    ip->ref++;
    printf("namei: found inode %d, ref count = %d\n", ip->inum, ip->ref);
    return ip;
}

struct inode* ialloc(uint32 dev, int16 type)
{
    printf("ialloc: allocating inode on device %d, type %d\n", dev, type);
    
    // 简化实现：查找空闲的 inode
    for (int i = 0; i < 100; i++) {
        struct inode *ip = &inode_table[i];
        if (ip->ref == 0) {
            // 初始化新 inode
            ip->dev = dev;
            ip->inum = i + 1;
            ip->ref = 1;
            ip->valid = 1;
            ip->type = type;
            ip->major = 0;
            ip->minor = 0;
            ip->nlink = 1;
            ip->size = 0;
            
            printf("ialloc: allocated inode %d\n", ip->inum);
            return ip;
        }
    }
    
    printf("ialloc: no available inodes\n");
    return NULL;
}

void ilock(struct inode *ip)
{
    if (!ip) {
        printf("ilock: null inode pointer\n");
        return;
    }
    
    acquiresleeplock(&ip->lock);
    printf("ilock: locked inode %d\n", ip->inum);
}

void iunlock(struct inode *ip)
{
    if (!ip) {
        printf("iunlock: null inode pointer\n");
        return;
    }
    
    if (!holdingsleeplock(&ip->lock)) {
        printf("iunlock: not holding lock for inode %d\n", ip->inum);
        return;
    }
    
    releasesleeplock(&ip->lock);
    printf("iunlock: unlocked inode %d\n", ip->inum);
}

void iput(struct inode *ip)
{
    if (!ip) {
        printf("iput: null inode pointer\n");
        return;
    }
    
    spinlock_acquire(&inode_lock);
    if (ip->ref < 1) {
        spinlock_release(&inode_lock);
        printf("iput: invalid reference count %d for inode %d\n", ip->ref, ip->inum);
        return;
    }
    
    ip->ref--;
    printf("iput: decremented ref count for inode %d to %d\n", ip->inum, ip->ref);
    
    if (ip->ref == 0) {
        // 清理 inode
        ip->valid = 0;
        ip->type = 0;
        ip->size = 0;
        printf("iput: freed inode %d\n", ip->inum);
    }
    
    spinlock_release(&inode_lock);
}

void iunlockput(struct inode *ip)
{
    iunlock(ip);
    iput(ip);
}

void iupdate(struct inode *ip)
{
    if (!ip) {
        printf("iupdate: null inode pointer\n");
        return;
    }
    
    printf("iupdate: updating inode %d (size=%d)\n", ip->inum, ip->size);
    // 简化实现：不做实际更新到磁盘
}

void itrunc(struct inode *ip)
{
    if (!ip) {
        printf("itrunc: null inode pointer\n");
        return;
    }
    
    printf("itrunc: truncating inode %d from size %d to 0\n", ip->inum, ip->size);
    ip->size = 0;
    iupdate(ip);
}

int namecmp(const char *s, const char *t)
{
    return strncmp(s, t, DIRSIZ);
}

// 睡眠锁实现
void initsleeplock(struct sleeplock *lk, char *name)
{
    spinlock_init(&lk->lk, "sleep lock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
}

void acquiresleeplock(struct sleeplock *lk)
{
    spinlock_acquire(&lk->lk);
    while (lk->locked) {
        // 简化实现：忙等待而不是睡眠
        spinlock_release(&lk->lk);
        // 在实际实现中这里应该调用 sleep()
        spinlock_acquire(&lk->lk);
    }
    lk->locked = 1;
    lk->pid = 1;  // 简化：使用固定PID
    spinlock_release(&lk->lk);
}

void releasesleeplock(struct sleeplock *lk)
{
    spinlock_acquire(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    // 在实际实现中这里应该调用 wakeup()
    spinlock_release(&lk->lk);
}

int holdingsleeplock(struct sleeplock *lk)
{
    int r;
    spinlock_acquire(&lk->lk);
    r = lk->locked && (lk->pid == 1);  // 简化：使用固定PID
    spinlock_release(&lk->lk);
    return r;
}

struct inode* idup(struct inode *ip) {
    if (!ip) {
        printf("idup: null inode pointer\n");
        return NULL;
    }
    
    spinlock_acquire(&inode_lock);
    if (ip->ref < 1) {
        spinlock_release(&inode_lock);
        printf("idup: invalid reference count %d for inode %d\n", ip->ref, ip->inum);
        return NULL;
    }
    
    ip->ref++;
    spinlock_release(&inode_lock);
    
    printf("idup: duplicated inode %d, new ref count = %d\n", ip->inum, ip->ref);
    return ip;
}
