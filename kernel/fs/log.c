#include "fs/log.h"
#include "fs/buf.h"
#include "fs/fs.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "mem/str.h"
#include "dev/vio.h"
#include "proc/cpu.h"

extern super_block_t sb;

// 日志系统状态
struct {
    spinlock_t lock;
    uint32 start;         // 日志区起始块号
    uint32 size;          // 日志区大小（块数）
    uint32 outstanding;   // 未完成的操作数
    bool committing;      // 是否正在提交
    log_header_t lh;      // 日志头（内存副本）
} log;

/*----------------------- 内部辅助函数 -------------------------*/

// 从日志区读取日志头
static void read_head(void)
{
    buf_t* buf = buf_read(log.start);
    log_header_t* lh = (log_header_t*)buf->data;
    
    log.lh.n = lh->n;
    for (uint32 i = 0; i < log.lh.n; i++) {
        log.lh.block[i] = lh->block[i];
    }
    
    buf_release(buf);
}

// 将内存中的日志头写回磁盘
// 这是提交的关键点：日志头写入表示事务提交
static void write_head(void)
{
    printf("write_head: starting, log.start=%d, log.lh.n=%d\n", log.start, log.lh.n);
    printf("write_head: calling buf_read for block %d\n", log.start);
    buf_t* buf = buf_read(log.start);
    printf("write_head: buf_read returned, block_num=%d\n", buf->block_num);
    
    log_header_t* lh = (log_header_t*)buf->data;
    
    lh->n = log.lh.n;
    printf("write_head: setting lh->n=%d\n", log.lh.n);
    for (uint32 i = 0; i < log.lh.n; i++) {
        lh->block[i] = log.lh.block[i];
        printf("write_head: setting lh->block[%d]=%d\n", i, log.lh.block[i]);
    }
    
    // 立即写回磁盘（不能通过日志系统）
    printf("write_head: calling virtio_disk_rw for block %d (write)\n", buf->block_num);
    virtio_disk_rw(buf, true);
    printf("write_head: virtio_disk_rw completed\n");
    
    printf("write_head: calling buf_release\n");
    buf_release(buf);
    printf("write_head: completed\n");
}


// 将日志块安装到文件系统
// 从日志区拷贝到实际的文件系统位置
static void install_trans(void)
{
    for (uint32 tail = 0; tail < log.lh.n; tail++) {
        // 读取日志块
        buf_t* lbuf = buf_read(log.start + tail + 1);  // +1 跳过日志头
        
        // 读取目标块
        buf_t* dbuf = buf_read(log.lh.block[tail]);
        
        // 拷贝数据
        memmove(dbuf->data, lbuf->data, BLOCK_SIZE);
        
        // 写回目标块（直接写磁盘，不通过日志）
        virtio_disk_rw(dbuf, true);
        
        buf_release(lbuf);
        buf_release(dbuf);
    }
}

// 从内存中的缓冲区拷贝到日志区
// 为提交做准备
static void write_log(void)
{
    printf("write_log: starting, n=%d\n", log.lh.n);
    for (uint32 tail = 0; tail < log.lh.n; tail++) {
        printf("write_log: processing block %d/%d (fs block=%d)\n", 
               tail + 1, log.lh.n, log.lh.block[tail]);
        
        // 读取日志块缓冲区
        printf("write_log: reading log block %d\n", log.start + tail + 1);
        buf_t* to = buf_read(log.start + tail + 1);  // +1 跳过日志头
        printf("write_log: read log block, block_num=%d\n", to->block_num);
        
        // 读取源数据块
        // ✅ 关键：从缓冲区缓存读取，确保读取到最新的数据
        // 如果缓冲区被释放了，buf_read 会从磁盘读取，但此时磁盘数据可能已过期
        // 所以我们需要确保缓冲区在提交前不被释放（通过增加引用计数）
        printf("write_log: reading fs block %d\n", log.lh.block[tail]);
        buf_t* from = buf_read(log.lh.block[tail]);
        printf("write_log: read fs block, block_num=%d\n", from->block_num);
        
        // ✅ 验证：确保读取的块号正确
        if (from->block_num != log.lh.block[tail]) {
            panic("write_log: block number mismatch");
        }
        
        // 拷贝数据
        printf("write_log: copying data\n");
        memmove(to->data, from->data, BLOCK_SIZE);
        
        // 写入日志区（直接写磁盘，不通过日志系统）
        printf("write_log: writing log block to disk\n");
        virtio_disk_rw(to, true);
        printf("write_log: disk write completed\n");
        
        buf_release(to);
        buf_release(from);
        printf("write_log: completed block %d/%d\n", tail + 1, log.lh.n);
    }
    printf("write_log: all blocks written\n");
}

// 提交事务
// 四步走：
// 1. write_log()      - 将修改的块写入日志区
// 2. write_head()     - 写日志头（提交点）
// 3. install_trans()  - 将日志安装到文件系统
// 4. write_head()     - 清除日志头（n=0）
// 5. release_pinned_buffers() - 释放被 pin 住的缓冲区引用
static void commit(void)
{
    printf("commit: starting, n=%d\n", log.lh.n);
    if (log.lh.n > 0) {
        // 保存块号列表，以便稍后释放缓冲区引用
        uint32 saved_blocks[LOG_SIZE];
        uint32 saved_n = log.lh.n;
        for (uint32 i = 0; i < saved_n; i++) {
            saved_blocks[i] = log.lh.block[i];
        }
        
        printf("commit: calling write_log()\n");
        write_log();       // 写日志数据块
        printf("commit: write_log() returned\n");
        
        printf("commit: calling write_head() (commit point)\n");
        write_head();      // 写日志头（提交点）
        printf("commit: write_head() (commit point) returned\n");
        
        printf("commit: calling install_trans()\n");
        install_trans();   // 安装到文件系统
        printf("commit: install_trans() returned\n");
        
        log.lh.n = 0;      // 清除日志
        printf("commit: calling write_head() (clear)\n");
        write_head();      // 写回空日志头
        printf("commit: write_head() (clear) returned\n");
        
        // 释放被 pin 住的缓冲区引用（使用保存的块号列表）
        printf("commit: releasing pinned buffers\n");
        for (uint32 i = 0; i < saved_n; i++) {
            buf_release_ref(saved_blocks[i]);
        }
        printf("commit: completed\n");
    } else {
        printf("commit: n=0, nothing to commit\n");
    }
}

/*----------------------- 崩溃恢复 -------------------------*/

// 从日志恢复
// 在文件系统初始化时调用
// 如果日志头的n>0，说明有未完成的事务，需要重放
static void recover_from_log(void)
{
    read_head();
    
    if (log.lh.n > 0) {
        printf("Recovering from log (%d blocks)...\n", log.lh.n);
        install_trans();   // 重放日志
        log.lh.n = 0;      // 清除日志
        write_head();      // 写回空日志头
        printf("Recovery complete\n");
    }
}

/*----------------------- 公共接口 -------------------------*/

// 初始化日志系统
// 在文件系统初始化时调用
void log_init(void)
{
    // printf("log_init: checking superblock configuration\n");
    // printf("  log_start = %d\n", sb.log_start);
    // printf("  log_blocks = %d\n", sb.log_blocks);
    
    // 检查超级块是否有日志信息
    if (sb.log_start == 0 || sb.log_blocks == 0) {
        panic("log_init: invalid log configuration in superblock");
    }
    
    // printf("log_init: initializing lock\n");
    spinlock_init(&log.lock, "log");
    
    log.start = sb.log_start;
    log.size = sb.log_blocks;
    log.outstanding = 0;
    log.committing = false;
    
    // 检查日志大小
    if (log.size > LOG_SIZE) {
        printf("log_init: warning: log size %d > LOG_SIZE %d\n", 
               log.size, LOG_SIZE);
        log.size = LOG_SIZE;
    }
    
    // printf("log_init: recovering from log\n");
    // 恢复未完成的事务
    recover_from_log();
    
    // printf("Log system initialized (start=%d, size=%d)\n", 
    //        log.start, log.size);
}

// 开始一个文件系统操作
// 调用者必须持有某些锁以确保原子性
// 可能会睡眠等待日志空间
void begin_op(void)
{
    spinlock_acquire(&log.lock);
    
    while (true) {
        // 如果正在提交，等待
        if (log.committing) {
            printf("begin_op: waiting for commit to finish (outstanding=%d, n=%d)\n", 
                   log.outstanding, log.lh.n);
            sleep(&log, &log.lock);
            continue;
        }
        
        // 如果日志空间不足，等待
        // 预留空间：当前日志块数 + 本次操作可能的最大块数
        if (log.lh.n + OP_MAXBLOCKS > log.size) {
            // 日志满了，等待提交
            printf("begin_op: waiting for log space (n=%d, size=%d, outstanding=%d)\n",
                   log.lh.n, log.size, log.outstanding);
            sleep(&log, &log.lock);
            continue;
        }
        
        // 可以开始操作
        log.outstanding++;
        spinlock_release(&log.lock);
        return;
    }
}

// 结束一个文件系统操作
// 如果是最后一个未完成的操作，触发提交
void end_op(void)
{
    bool do_commit = false;
    
    printf("end_op: entering, outstanding=%d, n=%d\n", log.outstanding, log.lh.n);
    
    spinlock_acquire(&log.lock);
    
    log.outstanding--;
    printf("end_op: outstanding decremented to %d\n", log.outstanding);
    
    if (log.committing) {
        panic("end_op: log.committing");
    }
    
    // 如果是最后一个操作，触发提交
    if (log.outstanding == 0) {
        do_commit = true;
        log.committing = true;
        printf("end_op: triggering commit, n=%d\n", log.lh.n);
    } else {
        // 唤醒等待的操作（可能有空间了）
        printf("end_op: not last op, waking up waiters\n");
        wakeup(&log);
    }
    
    spinlock_release(&log.lock);
    
    if (do_commit) {
        // 在锁外提交（避免持锁时间过长）
        printf("end_op: calling commit()\n");
        commit();
        printf("end_op: commit() returned\n");
        
        spinlock_acquire(&log.lock);
        log.committing = false;
        wakeup(&log);  // 唤醒所有等待的操作
        spinlock_release(&log.lock);
        printf("end_op: completed\n");
    }
}

// 记录一个块的写操作到日志
// 调用者必须持有buf的锁
// 这个函数实现了"写前日志"：先记录到日志，稍后再提交
void log_write(buf_t* b)
{
    //extern spinlock_t lk_buf_cache;
    
    // printf("log_write: block_num=%d, n=%d, outstanding=%d\n",
    //        b->block_num, log.lh.n, log.outstanding);
    
    if (log.lh.n >= log.size || log.lh.n >= LOG_SIZE) {
        panic("log_write: log is full");
    }
    
    if (log.outstanding < 1) {
        panic("log_write: outside of transaction");
    }
    
    // printf("log_write: acquiring log.lock\n");
    spinlock_acquire(&log.lock);
    // printf("log_write: acquired log.lock\n");
    
    // 检查这个块是否已经在日志中
    // 如果是，就不需要重复添加（合并写入优化）
    int i;
    for (i = 0; i < log.lh.n; i++) {
        if (log.lh.block[i] == b->block_num) {
            // 已经在日志中，不需要添加
            break;
        }
    }
    
    // 如果不在日志中，添加
    bool need_pin = (i == log.lh.n);
    if (need_pin) {
        log.lh.block[i] = b->block_num;
        log.lh.n++;
        // printf("log_write: added block %d to log, n=%d\n", b->block_num, log.lh.n);
    } else {
        // printf("log_write: block %d already in log\n", b->block_num);
    }
    
    // printf("log_write: releasing log.lock\n");
    spinlock_release(&log.lock);
    
    // ✅ Pin 住缓冲区：在日志锁外增加引用计数，防止在提交前被释放
    // 注意：只有在第一次添加到日志时才需要 pin（避免重复 pin）
    if (need_pin) {
        buf_pin(b);
    }
    
    // printf("log_write: completed\n");
}

/*----------------------- 调试辅助 -------------------------*/

// 打印日志状态（for debug）
void log_print(void)
{
    spinlock_acquire(&log.lock);
    
    printf("\nLog state:\n");
    printf("start = %d, size = %d\n", log.start, log.size);
    printf("outstanding = %d, committing = %d\n", 
           log.outstanding, log.committing);
    printf("log blocks (%d):\n", log.lh.n);
    
    for (uint32 i = 0; i < log.lh.n; i++) {
        printf("  log[%d] -> block %d\n", i, log.lh.block[i]);
    }
    
    spinlock_release(&log.lock);
}
