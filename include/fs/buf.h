#ifndef __BLOCK_BUF__
#define __BLOCK_BUF__

#include "common.h"
#include "lib/lock.h"
#include "memlayout.h"

typedef struct buf {
    /* 
        睡眠锁: 保护 data[BLOCK_SIZE] + disk
        block_num + buf_ref 由 lk_buf_cache保护
    */
    sleeplock_t slk;

    uint32 block_num; // 对应的磁盘block编号
    uint8  data[BLOCK_SIZE]; // block数据的缓存
    
    uint32 buf_ref; // 还有多少处引用没有释放 
    bool disk;      // 在磁盘驱动中使用

    bool dirty;     // 是否被修改过（需要写回）

} buf_t;

void   buf_init(void);
buf_t* buf_read(uint32 block_num);
void   buf_write(buf_t* buf);
void   buf_release(buf_t* buf);
void   buf_pin(buf_t* buf);                // Pin 住缓冲区（增加引用计数，用于日志系统）
void   buf_release_ref(uint32 block_num);  // 释放指定块号的缓冲区引用（用于日志系统）
void   buf_print(void);

void   buf_mark_dirty(buf_t* buf);  // 标记缓冲区为脏
void   buf_sync_all(void);          // 同步所有脏缓冲区到磁盘

#endif
