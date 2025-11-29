#ifndef __LOG_H__
#define __LOG_H__

#include "common.h"
#include "fs/buf.h"

// 日志配置
#define LOG_SIZE 30        // 日志块数量（足够容纳3个并发事务）
#define OP_MAXBLOCKS 10    // 单个操作最大块数

// 日志头结构（磁盘格式）
// 存储在日志区的第一个块
typedef struct log_header {
    uint32 n;                    // 当前日志中的块数
    uint32 block[LOG_SIZE];      // 每个日志块对应的文件系统块号
} log_header_t;

// 日志系统函数
void log_init(void);              // 初始化日志系统并恢复
void begin_op(void);              // 开始一个文件系统操作（事务）
void end_op(void);                // 结束一个文件系统操作（事务）
void log_write(struct buf* b);    // 记录一个块的写操作到日志
void log_print(void);

#endif
