#include "fs/buf.h"
#include "fs/log.h" 
#include "dev/vio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "mem/str.h"

#define N_BLOCK_BUF 64
#define BLOCK_NUM_UNUSED 0xFFFFFFFF

// 将buf包装成双向循环链表的node
struct buf_node {
    buf_t buf;
    struct buf_node* next;
    struct buf_node* prev;
};

// buf cache
static struct buf_node buf_cache[N_BLOCK_BUF];
static struct buf_node head_buf;
static spinlock_t lk_buf_cache;

// 链表操作
static void insert_head(struct buf_node* buf_node, bool head_next)
{
    // 离开原位置
    if(buf_node->next && buf_node->prev) {
        buf_node->next->prev = buf_node->prev;
        buf_node->prev->next = buf_node->next;
    }

    // 插入新位置
    if(head_next) { // 插入 head->next（MRU位置）
        buf_node->prev = &head_buf;
        buf_node->next = head_buf.next;
        head_buf.next->prev = buf_node;
        head_buf.next = buf_node;        
    } else { // 插入 head->prev（LRU位置）
        buf_node->next = &head_buf;
        buf_node->prev = head_buf.prev;
        head_buf.prev->next = buf_node;
        head_buf.prev = buf_node;
    }
}

// 初始化
void buf_init(void)
{
    spinlock_init(&lk_buf_cache, "buf_cache");
    
    // 初始化循环链表
    head_buf.next = &head_buf;
    head_buf.prev = &head_buf;
    
    // 初始化所有缓冲区
    for (int i = 0; i < N_BLOCK_BUF; i++) {
        buf_cache[i].buf.block_num = BLOCK_NUM_UNUSED;
        buf_cache[i].buf.buf_ref = 0;
        buf_cache[i].buf.disk = false;
        buf_cache[i].buf.dirty = false; 
        sleeplock_init(&buf_cache[i].buf.slk, "buffer");
        memset(buf_cache[i].buf.data, 0, BLOCK_SIZE);
        
        // 插入到空闲列表（head->prev）
        insert_head(&buf_cache[i], false);
    }
    
    printf("Buffer cache initialized (%d buffers)\n", N_BLOCK_BUF);
}

// 读取块（带缓存）
buf_t* buf_read(uint32 block_num)
{
    struct buf_node* node;
    buf_t* b;
    
    spinlock_acquire(&lk_buf_cache);
    
    // 1. 查找是否已在缓存中
    for (node = head_buf.next; node != &head_buf; node = node->next) {
        b = &node->buf;
        if (b->block_num == block_num) {
            // 缓存命中
            b->buf_ref++;
            spinlock_release(&lk_buf_cache);
            sleeplock_acquire(&b->slk);
            return b;
        }
    }
    
    // 块不在缓存中，需要查找空闲缓冲区
    // 2. 未命中，寻找空闲缓冲区（LRU）
    // 注意：不能在持有自旋锁时执行可能阻塞的操作（如磁盘I/O）
    struct buf_node* victim = NULL;
    bool victim_dirty = false;
    uint32 victim_block_num = BLOCK_NUM_UNUSED;
    
    for (node = head_buf.prev; node != &head_buf; node = node->prev) {
        b = &node->buf;
        if (b->buf_ref == 0) {
            victim = node;
            victim_dirty = b->dirty;
            victim_block_num = b->block_num;
            break;
        }
    }
    
    if (victim == NULL) {
        // 没有空闲缓冲区
        printf("buf_read: ERROR - no free buffers! block_num=%d\n", block_num);
        printf("buf_read: dumping buffer cache state:\n");
        int in_use = 0, pinned = 0;
        for (node = head_buf.next; node != &head_buf; node = node->next) {
            b = &node->buf;
            if (b->buf_ref > 0) {
                in_use++;
                if (b->buf_ref > 1) pinned++;
                printf("  buf block=%d, ref=%d, dirty=%d\n", 
                       b->block_num, b->buf_ref, b->dirty);
            }
        }
        printf("buf_read: buffers in use: %d, pinned: %d\n", in_use, pinned);
        spinlock_release(&lk_buf_cache);
        panic("buf_read: no free buffers (all buffers in use)");
        return NULL;
    }
    
    // 找到了候选缓冲区
    
    // 找到候选缓冲区，现在处理脏块写回（需要在锁外进行）
    b = &victim->buf;
    
    if (victim_dirty && victim_block_num != BLOCK_NUM_UNUSED) {
        // 先标记缓冲区为正在使用，防止被其他进程重用
        b->buf_ref = 1;
        uint32 old_block_num = b->block_num;
        spinlock_release(&lk_buf_cache);
        
        // 现在在锁外写回脏缓冲区
        sleeplock_acquire(&b->slk);
        virtio_disk_rw(b, true);
        b->dirty = false;
        sleeplock_release(&b->slk);
        
        // 重新获取自旋锁，检查缓冲区是否仍可用
        spinlock_acquire(&lk_buf_cache);
        if (b->buf_ref != 1 || b->block_num != old_block_num) {
            // 缓冲区状态改变了，释放引用并重新查找
            // printf("buf_read: victim state changed, retrying\n");
            if (b->buf_ref > 0) {
                b->buf_ref--;
            }
            spinlock_release(&lk_buf_cache);
            return buf_read(block_num);  // 递归重试
        }
        // 缓冲区仍然可用，继续使用
        // printf("buf_read: victim still available\n");
    }
    
    // 使用找到的空闲缓冲区
    b->block_num = block_num;
    b->buf_ref = 1;
    b->disk = false;
    b->dirty = false;
    
    // 移到MRU位置
    insert_head(victim, true);
    
    spinlock_release(&lk_buf_cache);
    sleeplock_acquire(&b->slk);
    
    // ✅ 从磁盘读取数据
    printf("buf_read: calling virtio_disk_rw for block %d (read)\n", block_num);
    virtio_disk_rw(b, false);
    printf("buf_read: virtio_disk_rw returned for block %d\n", block_num);
    
    return b;
}

// 写块到磁盘
void buf_write(buf_t* buf)
{
    if (!sleeplock_holding(&buf->slk)) {
        panic("buf_write: not holding lock");
    }
    
    // printf("buf_write: block_num=%d\n", buf->block_num);
    //vio_disk_rw(buf, true);
    //buf->dirty = false;
    log_write(buf);
    // printf("buf_write: log_write completed for block_num=%d\n", buf->block_num);
    // printf("buf_write: about to return\n");
    // printf("buf_write: returning now\n");
}

// 标记缓冲区为脏
void buf_mark_dirty(buf_t* buf)
{
    if (!sleeplock_holding(&buf->slk)) {
        panic("buf_mark_dirty: not holding lock");
    }
    buf->dirty = true;
}

// 同步所有脏缓冲区
void buf_sync_all(void)
{
    spinlock_acquire(&lk_buf_cache);
    
    for (int i = 0; i < N_BLOCK_BUF; i++) {
        buf_t* b = &buf_cache[i].buf;
        
        // 跳过未使用的缓冲区
        if (b->block_num == BLOCK_NUM_UNUSED) {
            continue;
        }
        
        // 如果是脏块，写回磁盘
        if (b->dirty) {
            // 需要获取睡眠锁
            sleeplock_acquire(&b->slk);
            virtio_disk_rw(b, true);
            b->dirty = false;
            sleeplock_release(&b->slk);
        }
    }
    
    spinlock_release(&lk_buf_cache);
    printf("buf_sync_all: all dirty blocks synced\n");
}

// 释放缓冲区
void buf_release(buf_t* buf)
{
    if (!sleeplock_holding(&buf->slk)) {
        panic("buf_release: not holding lock");
    }
    
    sleeplock_release(&buf->slk);
    
    spinlock_acquire(&lk_buf_cache);
    buf->buf_ref--;
    
    if (buf->buf_ref == 0) {
        // 计算 buf_node 的地址
        // buf_node 的 buf 成员在结构体开头，所以地址相同
        struct buf_node* node = (struct buf_node*)((char*)buf - 
            ((char*)&((struct buf_node*)0)->buf - (char*)0));
        
        // 移到LRU位置
        insert_head(node, false);
    }
    
    spinlock_release(&lk_buf_cache);
}

// Pin 住缓冲区（增加引用计数，用于日志系统）
// 防止缓冲区在事务提交前被释放
void buf_pin(buf_t* buf)
{
    spinlock_acquire(&lk_buf_cache);
    buf->buf_ref++;
    printf("buf_pin: pinned buffer for block %d, ref=%d\n", buf->block_num, buf->buf_ref);
    spinlock_release(&lk_buf_cache);
}

// 释放指定块号的缓冲区引用（用于日志系统的缓冲区 pin 机制）
// 只减少引用计数，不释放睡眠锁（因为可能没有持有锁）
void buf_release_ref(uint32 block_num)
{
    spinlock_acquire(&lk_buf_cache);
    
    // 在缓冲区缓存中查找这个块
    struct buf_node* node;
    for (node = &buf_cache[0]; node < &buf_cache[N_BLOCK_BUF]; node++) {
        if (node->buf.block_num == block_num && node->buf.buf_ref > 0) {
            // 找到缓冲区，释放一个引用计数
            node->buf.buf_ref--;
            
            // 如果引用计数变为 0，移到 LRU 位置
            if (node->buf.buf_ref == 0) {
                insert_head(node, false);
            }
            
            spinlock_release(&lk_buf_cache);
            return;
        }
    }
    
    spinlock_release(&lk_buf_cache);
}

// 输出buf_cache的情况
void buf_print(void)
{
    printf("\nbuf_cache:\n");
    struct buf_node* node = head_buf.next;
    spinlock_acquire(&lk_buf_cache);
    int count = 0;
    while(node != &head_buf && count < N_BLOCK_BUF)
    {
        buf_t* b = &node->buf;
        printf("buf %d: ref = %d, block_num = %d, dirty = %d\n",  
               (int)(node - buf_cache), b->buf_ref, b->block_num, b->dirty);
        for(int i = 0; i < 8; i++)
            printf("%d ", b->data[i]);
        printf("\n");
        node = node->next;
        count++;
    }
    spinlock_release(&lk_buf_cache);
}
