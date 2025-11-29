#ifndef __INODE_H__
#define __INODE_H__

#include "common.h"
#include "lib/lock.h"
#include "memlayout.h"

#define INODE_ROOT       0                              
#define INODE_DISK_SIZE  64                             
#define INODE_PER_BLOCK  (BLOCK_SIZE / INODE_DISK_SIZE) 

// addrs相关字段（调整后）
#define N_ADDRS_1   8   // 直接块：8 * 1KB = 8KB
#define N_ADDRS_2   2   // 一级间接：2 * 256 * 1KB = 512KB  
#define N_ADDRS_3   0   // 取消二级间接块（节省空间）
#define N_ADDRS     (N_ADDRS_1 + N_ADDRS_2)

#define ENTRY_PER_BLOCK (BLOCK_SIZE / sizeof(uint32))

// 最大文件大小：8KB + 512KB = 520KB（对于大部分场景足够）
#define INODE_MAXSIZE ((N_ADDRS_1 + N_ADDRS_2 * ENTRY_PER_BLOCK) * BLOCK_SIZE)

// type 选项
#define FT_UNUSED  0
#define FT_DIR     1
#define FT_FILE    2
#define FT_DEVICE  3
#define FT_SYMLINK 4

#define INODE_NUM_UNUSED 0xFFFF

// 磁盘inode结构
typedef struct inode_disk {
    // 文件元数据（8字节）
    uint16 type;                
    uint16 major;               
    uint16 minor;               
    uint16 nlink;               
    
    // 文件大小和时间戳（16字节）
    uint32 size;                
    uint32 atime;               
    uint32 mtime;               
    uint32 ctime;               
    
    // 块地址（40字节）
    uint32 addrs[N_ADDRS];      // 10个块指针
    
} inode_disk_t;

// 确保磁盘inode大小为64字节
_Static_assert(sizeof(inode_disk_t) == 64, "inode_disk_t must be 64 bytes");

// 内存inode结构
typedef struct inode {
    // 磁盘里的inode信息 (由slk保护)
    uint16 type;                
    uint16 major;               
    uint16 minor;               
    uint16 nlink;               
    uint32 size;                
    uint32 atime;               
    uint32 mtime;               
    uint32 ctime;               
    uint32 addrs[N_ADDRS];      

    // 内存里的inode信息
    uint16 inode_num;           
    uint32 ref;                 
    bool valid;                 
    sleeplock_t slk;            

} inode_t;

// inode 元数据

void     inode_init(void);                        
void     inode_rw(inode_t* ip, bool write);       
inode_t* inode_alloc(uint16 inode_num);           
inode_t* inode_create(uint16 type, uint16 major, uint16 minor); 
void     inode_free(inode_t* ip);                 
inode_t* inode_dup(inode_t* ip);                  
void     inode_lock(inode_t* ip);                 
void     inode_unlock(inode_t* ip);               
void     inode_unlock_free(inode_t* ip);          

// inode 管理的数据

uint32   inode_read_data(inode_t* ip, uint32 offset, uint32 len, void* dst, bool user);
uint32   inode_write_data(inode_t* ip, uint32 offset, uint32 len, void* src, bool user);
void     inode_free_data(inode_t* ip);

// 时间戳更新
void     inode_update_atime(inode_t* ip);  
void     inode_update_mtime(inode_t* ip);  

// for debug

void     inode_print(inode_t* ip);

#endif
