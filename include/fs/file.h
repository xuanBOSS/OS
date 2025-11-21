#ifndef __FILE_H__
#define __FILE_H__

#include "common.h"
#include "lib/lock.h"

// ================================
// 文件系统常量定义
// ================================

// 最大文件数和文件描述符数
#define NFILE       100     // 系统最大文件数
#define NOFILE      16      // 每个进程最大文件描述符数
#define MAXPATH     128     // 最大路径长度
#define DIRSIZ      14      // 目录名最大长度

// 设备号
#define NDEV        10      // 最大设备数
#define CONSOLE     1       // 控制台设备号

// 文件类型
#define FD_NONE     0
#define FD_PIPE     1
#define FD_INODE    2
#define FD_DEVICE   3

// 文件打开标志
#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002
#define O_CREATE    0x200
#define O_TRUNC     0x400

// inode 文件类型
#define T_DIR     1   // 目录
#define T_FILE    2   // 文件
#define T_DEVICE  3   // 设备

// ================================
// 数据结构定义
// ================================

// 睡眠锁结构（简化版）
struct sleeplock {
    spinlock_t lk;      // 保护睡眠锁的自旋锁
    int locked;         // 是否被锁定
    char *name;         // 锁的名称
    int pid;            // 持有锁的进程ID
};

// 管道结构（简化版）
struct pipe {
    spinlock_t lock;
    char data[512];
    uint32 nread;       // 读取的字节数
    uint32 nwrite;      // 写入的字节数
    int readopen;       // 读端是否打开
    int writeopen;      // 写端是否打开
};

// 文件结构
struct file {
    enum { FD_NONE_E, FD_PIPE_E, FD_INODE_E, FD_DEVICE_E } type;
    int ref;            // 引用计数
    char readable;      // 可读
    char writable;      // 可写
    struct pipe *pipe;  // FD_PIPE
    struct inode *ip;   // FD_INODE 和 FD_DEVICE
    uint32 off;         // FD_INODE
    int16 major;        // FD_DEVICE
};

// inode 结构（简化版）
struct inode {
    uint32 dev;         // 设备号
    uint32 inum;        // inode 号
    int ref;            // 引用计数
    struct sleeplock lock;
    int valid;          // inode 是否已从磁盘读取

    int16 type;         // 文件类型
    int16 major;        // 主设备号 (T_DEVICE only)
    int16 minor;        // 次设备号 (T_DEVICE only)
    int16 nlink;        // 链接数
    uint32 size;        // 文件大小（字节）
    uint32 addrs[13];   // 数据块地址
};

// 目录项
struct dirent {
    uint16 inum;
    char name[DIRSIZ];
};

// ================================
// 全局变量声明
// ================================

// 全局文件表
extern struct file ftable[NFILE];
extern spinlock_t ftable_lock;

// ================================
// 函数声明
// ================================

// 文件操作函数
struct file* filealloc(void);
void fileclose(struct file*);
struct file* filedup(struct file*);
void fileinit(void);
int fileread(struct file*, uint64, int n);
int filestat(struct file*, uint64 addr);
int filewrite(struct file*, uint64, int n);

// inode 相关函数
struct inode* ialloc(uint32, int16);
struct inode* idup(struct inode*);
void iinit(void);
void ilock(struct inode*);
void iput(struct inode*);
void iunlock(struct inode*);
void iunlockput(struct inode*);
void iupdate(struct inode*);
int namecmp(const char*, const char*);
struct inode* namei(char*);
struct inode* nameiparent(char*, char*);
int readi(struct inode*, int, uint64, uint32, uint32);
int writei(struct inode*, int, uint64, uint32, uint32);
void itrunc(struct inode*);

// 设备驱动函数
int console_read(int, uint64, int);
int console_write(int, uint64, int);

// 睡眠锁函数
void initsleeplock(struct sleeplock*, char*);
void acquiresleeplock(struct sleeplock*);
void releasesleeplock(struct sleeplock*);
int holdingsleeplock(struct sleeplock*);

// ================================
// 管道函数声明
// ================================
int pipealloc(struct file **f0, struct file **f1);
void pipeclose(struct pipe *pi, int writable);
int pipewrite(struct pipe *pi, uint64 addr, int n);
int piperead(struct pipe *pi, uint64 addr, int n);

#endif
