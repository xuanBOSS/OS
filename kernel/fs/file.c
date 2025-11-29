#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/dir.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/file.h"
#include "fs/log.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/lock.h"

// 外部函数声明
extern uint32 console_read(uint32 len, uint64 dst, bool user);
extern uint32 console_write(uint32 len, uint64 src, bool user);

// 设备列表(读写接口)
dev_t devlist[N_DEV];

// ftable + 保护它的锁
#define N_FILE 32
file_t ftable[N_FILE];
spinlock_t lk_ftable;

// ftable初始化 + devlist初始化
void file_init(void)
{
    spinlock_init(&lk_ftable, "ftable");
    
    // 初始化文件表
    for (int i = 0; i < N_FILE; i++) {
        ftable[i].type = FD_UNUSED;
        ftable[i].ref = 0;
    }
    
    // 初始化设备表
    for (int i = 0; i < N_DEV; i++) {
        devlist[i].read = NULL;
        devlist[i].write = NULL;
    }
    
    // 注册控制台设备
    devlist[DEV_CONSOLE].read = console_read;
    devlist[DEV_CONSOLE].write = console_write;
    
    printf("File system initialized\n");
}

// alloc file_t in ftable
// 失败则panic
file_t* file_alloc(void)
{
    spinlock_acquire(&lk_ftable);
    for (int i = 0; i < N_FILE; i++) {
        if (ftable[i].ref == 0) {
            ftable[i].ref = 1;
            ftable[i].type = FD_UNUSED;
            ftable[i].readable = false;
            ftable[i].writable = false;
            ftable[i].pipe = NULL;
            ftable[i].ip = NULL;
            ftable[i].offset = 0;
            ftable[i].major = 0;
            spinlock_release(&lk_ftable);
            return &ftable[i];
        }
    }
    spinlock_release(&lk_ftable);
    panic("file_alloc: no free file");
    return NULL;
}

// 创建设备文件(供proczero创建console)
file_t* file_create_dev(char* path, uint16 major, uint16 minor)
{
    // 创建设备 inode
    inode_t* ip = path_create_inode(path, FT_DEVICE, major, minor);
    if (ip == NULL) {
        return NULL;
    }
    
    // 分配文件结构
    file_t* f = file_alloc();
    f->type = FD_DEVICE;
    f->readable = true;
    f->writable = true;
    f->ip = ip;
    f->major = major;
    f->offset = 0;
    
    return f;
}

// 打开一个文件
file_t* file_open(char* path, uint32 open_mode)
{
    printf("file_open: path='%s', open_mode=0x%x\n", path, open_mode);
    inode_t* ip;
    
    // 如果需要创建且文件不存在
    if (open_mode & MODE_CREATE) {
        printf("file_open: MODE_CREATE set, calling path_create_inode\n");
        // ✅ 开始事务（path_create_inode 需要事务）
        begin_op();
        ip = path_create_inode(path, FT_FILE, 0, 0);
        printf("file_open: path_create_inode returned %p\n", ip);
        if (ip == NULL) {
            end_op();  // ✅ 失败时结束事务
            printf("file_open: failed to get inode for '%s'\n", path);
            return NULL;
        }
        // ✅ 注意：不要释放 ip，因为我们需要使用它来创建 file_t
        // 事务结束后，inode 仍然有效（引用计数 > 0）
        end_op();  // ✅ 成功时结束事务
    } else {
        printf("file_open: no MODE_CREATE, calling path_to_inode\n");
        ip = path_to_inode(path);
        printf("file_open: path_to_inode returned %p\n", ip);
        if (ip == NULL) {
            printf("file_open: failed to get inode for '%s'\n", path);
            return NULL;
        }
    }
    
    inode_lock(ip);
    
    // 分配文件结构
    file_t* f = file_alloc();
    if (f == NULL) {
        inode_unlock(ip);
        inode_free(ip);
        return NULL;
    }
    
    f->type = (ip->type == FT_DIR) ? FD_DIR : FD_FILE;
    f->ip = ip;
    f->offset = 0;
    f->readable = (open_mode & MODE_READ) != 0;
    f->writable = (open_mode & MODE_WRITE) != 0;
    
    // ✅ 返回前解锁（文件打开后不应该持有 inode 锁）
    inode_unlock(ip);
    
    return f;
}

// 释放一个file
void file_close(file_t* file)
{
    if (!file) {
        return;
    }
    
    spinlock_acquire(&lk_ftable);
    
    if (file->ref < 1) {
        spinlock_release(&lk_ftable);
        panic("file_close: ref < 1");
    }
    
    // printf("🔒 file_close:\n");
    // printf("   file->ref=%d->%d, type=%d\n",
    //        file->ref, file->ref - 1, file->type);
    if (file->type == FD_FILE || file->type == FD_DIR) {
        // printf("   inode_num=%d\n", file->ip ? file->ip->inode_num : -1);
    }
    
    file->ref--;
    
    if (file->ref > 0) {
        // printf("   Still has refs, not closing\n");
        spinlock_release(&lk_ftable);
        return;
    }
    
    // printf("   Last ref, closing...\n");
    
    // 引用计数为 0，需要释放资源
    file_t ff = *file;
    file->type = FD_UNUSED;
    
    spinlock_release(&lk_ftable);
    
    // 根据文件类型释放资源
    if (ff.type == FD_PIPE) {
        pipeclose(ff.pipe, ff.writable);
    } else if (ff.type == FD_FILE || ff.type == FD_DIR) {
        if (ff.ip) {
            printf("   Calling inode_free for inode %d\n", ff.ip->inode_num);
            inode_free(ff.ip);
        }
    } else if (ff.type == FD_DEVICE) {
        if (ff.ip) {
            inode_free(ff.ip);
        }
    }
}

// 文件内容读取
// 返回读取到的字节数
uint32 file_read(file_t* file, uint32 len, uint64 dst, bool user)
{
    if (!file->readable) {
        return 0;
    }
    
    // 管道读取
    if (file->type == FD_PIPE) {
        return piperead(file->pipe, dst, len);
    }
    
    // 设备读取
    if (file->type == FD_DEVICE) {
        if (file->major >= N_DEV || devlist[file->major].read == NULL) {
            return 0;
        }
        return devlist[file->major].read(len, dst, user);
    }
    
    // 文件/目录读取
    if (file->type == FD_FILE || file->type == FD_DIR) {
        inode_lock(file->ip);
        // printf("file_read: inode_num=%d, offset=%d, len=%d, size=%d\n", 
        //        file->ip->inode_num, file->offset, len, file->ip->size);
        uint32 r = inode_read_data(file->ip, file->offset, len, (void*)dst, user);
        // printf("file_read: inode_read_data returned %d\n", r);
        if (r > 0) {
            file->offset += r;
            // 更新访问时间
            inode_update_atime(file->ip);
        }
        inode_unlock(file->ip);
        return r;
    }
    
    return 0;
}

// 文件内容写入
// 返回写入的字节数
uint32 file_write(file_t* file, uint32 len, uint64 src, bool user)
{
    if (!file->writable) {
        return 0;
    }
    
    // 管道写入
    if (file->type == FD_PIPE) {
        return pipewrite(file->pipe, src, len);
    }
    
    // 设备写入
    if (file->type == FD_DEVICE) {
        if (file->major >= N_DEV || devlist[file->major].write == NULL) {
            return 0;
        }
        return devlist[file->major].write(len, src, user);
    }
    
    // 文件写入
    if (file->type == FD_FILE) {
        inode_lock(file->ip);
        uint32 r = inode_write_data(file->ip, file->offset, len, (void*)src, user);
        if (r > 0) {
            file->offset += r;
            // inode_write_data 内部已更新修改时间
        }
        inode_unlock(file->ip);
        return r;
    }
    
    // 目录不可写
    return 0;
}

// flags 可能取值
#define LSEEK_SET 0  // file->offset = offset
#define LSEEK_ADD 1  // file->offset += offset
#define LSEEK_SUB 2  // file->offset -= offset

// 修改file->offset (只针对FD_FILE类型的文件)
uint32 file_lseek(file_t* file, uint32 offset, int flags)
{
    if (file->type != FD_FILE) {
        return -1;
    }
    
    switch (flags) {
        case LSEEK_SET:
            file->offset = offset;
            break;
        case LSEEK_ADD:
            file->offset += offset;
            break;
        case LSEEK_SUB:
            if (offset > file->offset) {
                file->offset = 0;
            } else {
                file->offset -= offset;
            }
            break;
        default:
            return -1;
    }
    
    return file->offset;
}

// file->ref++ with lock
file_t* file_dup(file_t* file)
{
    spinlock_acquire(&lk_ftable);
    assert(file->ref > 0, "file_dup: ref");
    file->ref++;
    spinlock_release(&lk_ftable);
    return file;
}

// 获取文件状态
int file_stat(file_t* file, uint64 addr)
{
    file_state_t state;
    if(file->type == FD_FILE || file->type == FD_DIR)
    {
        inode_lock(file->ip);
        state.type = file->ip->type;
        state.inode_num = file->ip->inode_num;
        state.nlink = file->ip->nlink;
        state.size = file->ip->size;
        inode_unlock(file->ip);

        uvm_copyout(myproc()->pgtbl, addr, (uint64)&state, sizeof(file_state_t));
        return 0;
    }
    return -1;
}
