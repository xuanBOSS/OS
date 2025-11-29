#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/fs.h"
#include "fs/log.h" 
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "mem/str.h"

extern super_block_t sb;

// 内存中的inode资源 + 保护它的锁
#define N_INODE 32
static inode_t icache[N_INODE];
static spinlock_t lk_icache;

// icache初始化
void inode_init(void)
{
    spinlock_init(&lk_icache, "icache");
    
    for (int i = 0; i < N_INODE; i++) {
        sleeplock_init(&icache[i].slk, "inode");
        icache[i].ref = 0;
        icache[i].valid = false;
    }
    
    printf("Inode cache initialized\n");
}

/*---------------------- 与inode本身相关 -------------------*/

// 获取当前时间戳（简化版）
static uint32 get_time(void)
{
    static uint32 time_counter = 0;
    return ++time_counter;
}

// 使用磁盘里的inode更新内存里的inode (write = false)
// 或 使用内存里的inode更新磁盘里的inode (write = true)
// 调用者需要设置inode_num并持有睡眠锁
void inode_rw(inode_t* ip, bool write)
{
    if (!sleeplock_holding(&ip->slk)) {
        panic("inode_rw: not holding lock");
    }
    
    // 计算inode在磁盘上的位置
    uint32 block_num = sb.inode_start + ip->inode_num / INODE_PER_BLOCK;
    uint32 offset = (ip->inode_num % INODE_PER_BLOCK) * INODE_DISK_SIZE;
    
    // printf("inode_rw: inode_num=%d, block_num=%d, offset=%d, write=%d\n",
    //        ip->inode_num, block_num, offset, write);
    
    buf_t* buf = buf_read(block_num);
    // printf("inode_rw: buf_read completed\n");
    inode_disk_t* dip = (inode_disk_t*)(buf->data + offset);
    
    // 调试输出：检查磁盘上的 addrs[0]
    if (ip->inode_num == 0 && !write) {
        printf("inode_rw: reading inode 0 from disk, dip->addrs[0]=%d (raw)\n", dip->addrs[0]);
    }
    
    if (write) {
        // 内存 → 磁盘
        // printf("inode_rw: writing inode data\n");
        dip->type = ip->type;
        dip->major = ip->major;
        dip->minor = ip->minor;
        dip->nlink = ip->nlink;
        dip->size = ip->size;
        dip->atime = ip->atime;
        dip->mtime = ip->mtime;
        dip->ctime = ip->ctime;
        memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
        // printf("inode_rw: calling buf_write\n");
        // printf("inode_rw: before buf_write call\n");
        buf_write(buf);
        // printf("inode_rw: after buf_write call\n");
        // printf("inode_rw: buf_write returned\n");
        // printf("inode_rw: buf_write completed\n");
    } else {
        // 磁盘 → 内存
        ip->type = dip->type;
        ip->major = dip->major;
        ip->minor = dip->minor;
        ip->nlink = dip->nlink;
        ip->size = dip->size;
        ip->atime = dip->atime;
        ip->mtime = dip->mtime;
        ip->ctime = dip->ctime;
        memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
        
        // 调试输出
        if (ip->inode_num == 0) {
            printf("inode_rw: read inode 0, type=%d, size=%d, addrs[0]=%d\n", 
                   ip->type, ip->size, ip->addrs[0]);
        }
    }
    
    // printf("inode_rw: calling buf_release\n");
    buf_release(buf);
    // printf("inode_rw: completed\n");
}

// 在icache里查询inode
// 如果没有查询到则申请一个空闲inode
// 如果icache没有空闲inode则报错
// 注意: 获得的inode没有上锁
inode_t* inode_alloc(uint16 inode_num)
{    
    inode_t* ip;
    inode_t* empty = NULL;
    
    spinlock_acquire(&lk_icache);
    
    // 1. 查找是否已在缓存中
    for (ip = &icache[0]; ip < &icache[N_INODE]; ip++) {
        if (ip->ref > 0 && ip->inode_num == inode_num) {
            // 缓存命中
            ip->ref++;
            spinlock_release(&lk_icache);
            return ip;
        }
        if (empty == NULL && ip->ref == 0) {
            empty = ip;  // 记录空闲槽位
        }
    }
    
    // 2. 未命中，使用空闲槽位
    if (empty == NULL) {
        spinlock_release(&lk_icache);
        panic("inode_alloc: no free inodes in cache");
    }
    
    ip = empty;
    ip->inode_num = inode_num;
    ip->ref = 1;
    ip->valid = false;

    ip->type = FT_UNUSED;
    ip->nlink = 0;
    ip->size = 0;
    ip->major = 0;
    ip->minor = 0;
    memset(ip->addrs, 0, sizeof(ip->addrs));
    
    spinlock_release(&lk_icache);
    return ip;
}

// 在磁盘里申请一个inode (操作bitmap, 返回inode_num)
// 向icache申请一个inode数据结构
// 填写内存里的inode并以此更新磁盘里的inode
// 注意: 获得的inode没有上锁
// ✅ 调用者负责事务管理
inode_t* inode_create(uint16 type, uint16 major, uint16 minor)
{
    // ✅ 移除这里的 begin_op()，由调用者管理
    // begin_op();

    // 1. 在磁盘上分配inode
    uint16 inode_num = bitmap_alloc_inode();
    if (inode_num == 0xFFFF) {
        // ✅ 移除 end_op()
        // end_op();
        printf("inode_create: no free inodes\n");
        return NULL;
    }
    
    // 2. 在内存中分配inode
    inode_t* ip = inode_alloc(inode_num);
    
    // 3. 初始化inode
    sleeplock_acquire(&ip->slk);
    ip->type = type;
    ip->major = major;
    ip->minor = minor;
    ip->nlink = 1;  // ✅ 保持为 1
    ip->size = 0;
    uint32 now = get_time();
    ip->ctime = now;
    ip->mtime = now;
    ip->atime = now;
    memset(ip->addrs, 0, sizeof(ip->addrs));
    ip->valid = true;

    // printf("🆕 inode_create: creating inode %d, type=%d, nlink=%d\n",
    //        inode_num, type, ip->nlink);
    
    // 4. 写回磁盘
    // printf("🆕 inode_create: calling inode_rw(ip, true) for inode %d\n", inode_num);
    inode_rw(ip, true);
    // printf("🆕 inode_create: inode_rw completed for inode %d\n", inode_num);
    sleeplock_release(&ip->slk);
    // printf("🆕 inode_create: completed for inode %d\n", inode_num);

    // ✅ 移除这里的 end_op()
    // end_op();
    
    return ip;
}

// 供inode_free调用
// 在磁盘上删除一个inode及其管理的文件 (修改inode bitmap + block bitmap)
// 调用者需要持有lk_icache, 但不应该持有slk
static void inode_destroy(inode_t* ip)
{
    if (!spinlock_holding(&lk_icache)) {
        panic("inode_destroy: not holding lk_icache");
    }
    
    if (sleeplock_holding(&ip->slk)) {
        panic("inode_destroy: holding slk");
    }

    begin_op(); 
    
    sleeplock_acquire(&ip->slk);
    
    // 释放数据块
    inode_free_data(ip);
    
    // 清除inode
    ip->type = FT_UNUSED;
    ip->size = 0;
    inode_rw(ip, true);
    
    // 释放inode位图
    bitmap_free_inode(ip->inode_num);
    
    ip->valid = false;
    sleeplock_release(&ip->slk);

    end_op();
}

// 向icache里归还inode
// inode->ref--
// 调用者不应该持有slk
void inode_free(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    
    if (ip->ref < 1) {
        panic("inode_free: ref < 1");
    }
    
    // ✅ 详细的调试信息
    // printf("📍 inode_free CALLED: inode_num=%d, ref=%d, valid=%d, nlink=%d\n",
    //        ip->inode_num, ip->ref, ip->valid, ip->nlink);
    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        // 最后一个引用且无硬链接 → 销毁
        // printf("📍 inode_free: destroying inode %d (ref=1, nlink=0)\n", ip->inode_num);
        inode_destroy(ip);
    }
    
    ip->ref--;
    
    spinlock_release(&lk_icache);
}

// ip->ref++ with lock
inode_t* inode_dup(inode_t* ip)
{
    spinlock_acquire(&lk_icache);
    assert(ip->ref > 0, "inode_dup: ref");
    ip->ref++;
    spinlock_release(&lk_icache);
    return ip;
}

// 给inode上锁
// 如果valid失效则从磁盘中读入
void inode_lock(inode_t* ip)
{
    if (ip == NULL || ip->ref < 1) {
        panic("inode_lock: invalid inode");
    }
    
    printf("inode_lock: acquiring lock for inode %d\n", ip->inode_num);
    sleeplock_acquire(&ip->slk);
    printf("inode_lock: lock acquired for inode %d, valid=%d\n", ip->inode_num, ip->valid);
    
    if (!ip->valid) {
        // 从磁盘读取
        printf("inode_lock: reading inode %d from disk\n", ip->inode_num);
        inode_rw(ip, false);
        ip->valid = true;
        printf("inode_lock: inode %d read from disk, type=%d\n", ip->inode_num, ip->type);
        
        if (ip->type == FT_UNUSED) {
            panic("inode_lock: inode type is unused");
        }
    }
    printf("inode_lock: completed for inode %d\n", ip->inode_num);
}

// 给inode解锁
void inode_unlock(inode_t* ip)
{
    if (ip == NULL || !sleeplock_holding(&ip->slk) || ip->ref < 1) {
        panic("inode_unlock: invalid unlock");
    }
    
    sleeplock_release(&ip->slk);
}

// 连招: 解锁 + 释放
void inode_unlock_free(inode_t* ip)
{
    if (!ip) {
        return;
    }
    
    // ✅ 检查是否真的持有锁
    if (sleeplock_holding(&ip->slk)) {
        inode_unlock(ip);
    }
    
    inode_free(ip);
}

/*---------------------------- 与inode管理的data相关 --------------------------*/

// 更新访问时间
void inode_update_atime(inode_t* ip)
{
    if (!sleeplock_holding(&ip->slk)) {
        panic("inode_update_atime: not holding lock");
    }
    
    ip->atime = get_time();
    inode_rw(ip, true);
}

// 更新修改时间
void inode_update_mtime(inode_t* ip)
{
    if (!sleeplock_holding(&ip->slk)) {
        panic("inode_update_mtime: not holding lock");
    }
    
    ip->mtime = get_time();
    inode_rw(ip, true);
}

// 辅助 inode_locate_block
// 递归查询或创建block
static uint32 locate_block(uint32* entry, uint32 bn, uint32 size)
{
    if(*entry == 0)
        *entry = bitmap_alloc_block();

    if(size == 1)
        return *entry;    

    uint32* next_entry;
    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn = bn % next_size;
    uint32 ret = 0;

    buf_t* buf = buf_read(*entry);
    next_entry = (uint32*)(buf->data) + bn / next_size;
    ret = locate_block(next_entry, next_bn, next_size);
    
    buf_write(buf);  // 现在会调用 log_write()
    buf_release(buf);

    return ret;
}

// 辅助函数：获取块号（不分配，用于读取）
static uint32 get_block(uint32 entry, uint32 bn, uint32 size)
{
    if (entry == 0) {
        return 0;  // 块未分配
    }
    
    if (size == 1) {
        return entry;
    }
    
    uint32 next_size = size / ENTRY_PER_BLOCK;
    uint32 next_bn = bn % next_size;
    
    buf_t* buf = buf_read(entry);
    uint32* next_entry = (uint32*)(buf->data) + bn / next_size;
    uint32 ret = get_block(*next_entry, next_bn, next_size);
    buf_release(buf);
    
    return ret;
}

// 获取inode里第bn块data block的block_num（不分配，用于读取）
// 如果块未分配，返回0
static uint32 inode_get_block(inode_t* ip, uint32 bn)
{
    assert(sleeplock_holding(&ip->slk), "inode_get_block: lock");
    
    // 直接块：addrs[0-7] 管理 8KB
    if (bn < N_ADDRS_1) {
        return ip->addrs[bn];
    }
    
    bn -= N_ADDRS_1;
    
    // 一级间接块：addrs[8-9] 管理 512KB
    if (bn < N_ADDRS_2 * ENTRY_PER_BLOCK) {
        uint32 idx = N_ADDRS_1 + bn / ENTRY_PER_BLOCK;
        uint32 offset = bn % ENTRY_PER_BLOCK;
        if (ip->addrs[idx] == 0) {
            return 0;
        }
        return get_block(ip->addrs[idx], offset, ENTRY_PER_BLOCK);
    }
    
    return 0;
}

// 确定inode里第bn块data block的block_num
// 如果不存在第bn块data block则申请一个并返回它的block_num
// 由于inode->addrs的结构, 这个过程比较复杂, 需要单独处理
static uint32 inode_locate_block(inode_t* ip, uint32 bn)
{
    assert(sleeplock_holding(&ip->slk), "inode_locate_block: lock");
    
    printf("inode_locate_block: bn=%d, ip->inode_num=%d\n", bn, ip->inode_num);
    
    // 直接块：addrs[0-7] 管理 8KB
    if (bn < N_ADDRS_1) {
        printf("inode_locate_block: direct block, addrs[%d]=%d\n", bn, ip->addrs[bn]);
        if (ip->addrs[bn] == 0) {
            printf("inode_locate_block: calling bitmap_alloc_block\n");
            ip->addrs[bn] = bitmap_alloc_block();
            printf("inode_locate_block: bitmap_alloc_block returned %d\n", ip->addrs[bn]);
            if (ip->addrs[bn] == 0) {
                panic("inode_locate_block: no free blocks");
            }
            // 注意：addrs修改会在 inode_write_data 结束时通过 inode_update_mtime 写回
        }
        printf("inode_locate_block: returning block_num=%d\n", ip->addrs[bn]);
        return ip->addrs[bn];
    }
    
    bn -= N_ADDRS_1;
    
    // 一级间接块：addrs[8-9] 管理 512KB
    if (bn < N_ADDRS_2 * ENTRY_PER_BLOCK) {
        uint32 idx = N_ADDRS_1 + bn / ENTRY_PER_BLOCK;
        uint32 offset = bn % ENTRY_PER_BLOCK;
        return locate_block(&ip->addrs[idx], offset, ENTRY_PER_BLOCK);
    }
    
    panic("inode_locate_block: block number out of range");
    return 0;
}

// 读取 inode 管理的 data block
// 调用者需要持有 inode 锁
// 成功返回读出的字节数, 失败返回0
uint32 inode_read_data(inode_t* ip, uint32 offset, uint32 len, void* dst, bool user)
{
    assert(sleeplock_holding(&ip->slk), "inode_read_data: lock");
    
    // printf("inode_read_data: inode_num=%d, offset=%d, len=%d, size=%d\n", 
    //        ip->inode_num, offset, len, ip->size);
    
    if (offset > ip->size || offset + len < offset) {
        return 0;
    }
    
    if (offset + len > ip->size) {
        len = ip->size - offset;
        // printf("inode_read_data: adjusted len to %d\n", len);
    }
    
    uint32 total = 0;
    uint32 iter_count = 0;
    while (total < len) {
        iter_count++;
        if (iter_count > 10) {
            printf("inode_read_data: WARNING - too many iterations (%d), breaking\n", iter_count);
            break;
        }
        
        uint32 bn = offset / BLOCK_SIZE;
        uint32 block_offset = offset % BLOCK_SIZE;
        // ✅ 对于读取操作，如果块未分配，应该返回 0 而不是分配新块
        // printf("inode_read_data: calling inode_get_block for bn=%d\n", bn);
        uint32 block_num = inode_get_block(ip, bn);
        // printf("inode_read_data: inode_get_block returned block_num=%d\n", block_num);
        
        if (block_num == 0) {
            // printf("inode_read_data: block not allocated, breaking\n");
            break;
        }
        
        // printf("inode_read_data: calling buf_read for block %d\n", block_num);
        buf_t* buf = buf_read(block_num);
        // printf("inode_read_data: buf_read returned\n");
        
        uint32 n = BLOCK_SIZE - block_offset;
        if (n > len - total) {
            n = len - total;
        }
        
        // printf("inode_read_data: copying %d bytes\n", n);
        if (user) {
            uvm_copyout(myproc()->pgtbl, (uint64)dst + total, 
                       (uint64)(buf->data + block_offset), n);
        } else {
            memmove((char*)dst + total, buf->data + block_offset, n);
        }
        
        // printf("inode_read_data: calling buf_release\n");
        buf_release(buf);
        // printf("inode_read_data: buf_release completed\n");
        
        total += n;
        offset += n;
        // printf("inode_read_data: total=%d, offset=%d, len=%d\n", total, offset, len);
    }
    
    // printf("inode_read_data: returning total=%d\n", total);
    
    return total;
}

// 写入 inode 管理的 data block (可能导致管理的 block 增加)
// 调用者需要持有 inode 锁
// 成功返回写入的字节数, 失败返回0
uint32 inode_write_data(inode_t* ip, uint32 offset, uint32 len, void* src, bool user)
{
    assert(sleeplock_holding(&ip->slk), "inode_write_data: lock");
    
    if (offset > ip->size || offset + len < offset) {
        return 0;
    }
    
    // 检查文件大小限制
    if (offset + len > INODE_MAXSIZE) {
        printf("inode_write_data: file too large\n");
        return 0;
    }
    
    uint32 total = 0;
    printf("inode_write_data: offset=%d, len=%d, ip->inode_num=%d\n", offset, len, ip->inode_num);
    while (total < len) {
        uint32 bn = offset / BLOCK_SIZE;
        uint32 block_offset = offset % BLOCK_SIZE;
        printf("inode_write_data: calling inode_locate_block, bn=%d\n", bn);
        uint32 block_num = inode_locate_block(ip, bn);
        printf("inode_write_data: inode_locate_block returned block_num=%d\n", block_num);
        
        if (block_num == 0) {
            printf("inode_write_data: failed to allocate block\n");
            break;
        }
        
        printf("inode_write_data: calling buf_read for block %d\n", block_num);
        buf_t* buf = buf_read(block_num);
        printf("inode_write_data: buf_read returned\n");
        uint32 n = BLOCK_SIZE - block_offset;
        if (n > len - total) {
            n = len - total;
        }
        
        if (user) {
            uvm_copyin(myproc()->pgtbl, (uint64)(buf->data + block_offset),
                      (uint64)src + total, n);
        } else {
            memmove(buf->data + block_offset, (char*)src + total, n);
        }
        
        buf_mark_dirty(buf);
        buf_release(buf);
        
        total += n;
        offset += n;
    }
    
    // 更新文件大小
    if (offset > ip->size) {
        ip->size = offset;
    }
    
    // 更新修改时间
    inode_update_mtime(ip);
    
    return total;
}

// 辅助 inode_free_data 做递归释放
static void data_free(uint32 block_num, uint32 level)
{  
    assert(block_num != 0, "data_free: block_num = 0");

    // block_num 是 data block
    if(level == 0) {
        bitmap_free_block(block_num);
        return;
    }

    // block_num 是 metadata block (间接块)
    buf_t* buf = buf_read(block_num);
    for(uint32* addr = (uint32*)buf->data; addr < (uint32*)(buf->data + BLOCK_SIZE); addr++) 
    {
        if(*addr == 0) break;
        data_free(*addr, level - 1);
    }
    buf_release(buf);

    bitmap_free_block(block_num);
}

// 释放inode管理的 data block
// ip->addrs被清空 ip->size置0
// 调用者需要持有slk
void inode_free_data(inode_t* ip)
{
    assert(sleeplock_holding(&ip->slk), "inode_free_data: lock");
    
    // 释放直接块
    for (int i = 0; i < N_ADDRS_1; i++) {
        if (ip->addrs[i] != 0) {
            bitmap_free_block(ip->addrs[i]);
            ip->addrs[i] = 0;
        }
    }
    
    // 释放一级间接块
    for (int i = N_ADDRS_1; i < N_ADDRS_1 + N_ADDRS_2; i++) {
        if (ip->addrs[i] != 0) {
            data_free(ip->addrs[i], 1);
            ip->addrs[i] = 0;
        }
    }
    
    ip->size = 0;
    inode_rw(ip, true);
}

static char* inode_types[] = {
    "INODE_UNUSED",
    "INODE_DIR",
    "INODE_FILE",
    "INODE_DEVICE",
    "INODE_SYMLINK",
};

// 输出inode信息
// for debug
void inode_print(inode_t* ip)
{
    assert(sleeplock_holding(&ip->slk), "inode_print: lk");

    printf("\ninode information:\n");
    printf("num = %d, ref = %d, valid = %d\n", ip->inode_num, ip->ref, ip->valid);
    printf("type = %s, major = %d, minor = %d, nlink = %d\n", 
           inode_types[ip->type], ip->major, ip->minor, ip->nlink);
    printf("size = %d\n", ip->size);
    printf("atime = %d, mtime = %d, ctime = %d\n", ip->atime, ip->mtime, ip->ctime);
    printf("addrs =");
    for(int i = 0; i < N_ADDRS; i++)
        printf(" %d", ip->addrs[i]);
    printf("\n");
}
