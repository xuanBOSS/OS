#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/log.h" 
#include "fs/bitmap.h"
#include "lib/print.h"
#include "fs/inode.h"

extern super_block_t sb;

// 在位图中搜索空闲bit并设置
// 返回bit的序号（从0开始）
// 如果没有空闲bit，返回0xFFFFFFFF
static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    printf("bitmap_search_and_set: starting, bitmap_block=%d\n", bitmap_block);
    printf("bitmap_search_and_set: calling buf_read\n");
    buf_t* buf = buf_read(bitmap_block);
    printf("bitmap_search_and_set: buf_read returned, block_num=%d\n", buf->block_num);
    
    for (uint32 byte = 0; byte < BLOCK_SIZE; byte++) {
        uint8 bit_cmp = 1;
        for (uint32 shift = 0; shift < 8; shift++) {
            // 检查bit是否为0（空闲）
            if ((buf->data[byte] & bit_cmp) == 0) {
                // 设置bit为1（已分配）
                printf("bitmap_search_and_set: found free bit at byte=%d, shift=%d\n", byte, shift);
                buf->data[byte] |= bit_cmp;
                printf("bitmap_search_and_set: calling buf_write\n");
                buf_write(buf);  // 写回磁盘
                printf("bitmap_search_and_set: buf_write returned\n");
                buf_release(buf);
                printf("bitmap_search_and_set: returning bit_num=%d\n", byte * 8 + shift);
                return byte * 8 + shift;
            }
            bit_cmp = bit_cmp << 1;
        }
    }
    
    printf("bitmap_search_and_set: no free bits found\n");
    buf_release(buf);
    return 0xFFFFFFFF;  // 没有空闲bit
}

// 清除位图中的bit
static void bitmap_unset(uint32 bitmap_block, uint32 num)
{
    uint32 byte = num / 8;
    uint32 shift = num % 8;
    uint8 bit_cmp = 1 << shift;
    
    buf_t* buf = buf_read(bitmap_block);
    
    // 检查bit是否已经设置
    if ((buf->data[byte] & bit_cmp) == 0) {
        buf_release(buf);
        panic("bitmap_unset: bit not set");
    }
    
    // 清除bit
    buf->data[byte] &= ~bit_cmp;
    buf_write(buf);
    buf_release(buf);
}

// 分配一个数据块
// 返回块号，失败返回0
uint32 bitmap_alloc_block(void)
{
    uint32 bit_num = bitmap_search_and_set(sb.data_bitmap_start);
    
    if (bit_num == 0xFFFFFFFF) {
        printf("bitmap_alloc_block: no free blocks\n");
        return 0;
    }
    
    // bit_num转换为实际的块号
    uint32 block_num = bit_num + sb.data_start;
    
    // 检查是否超出范围
    if (block_num >= sb.data_start + sb.data_blocks) {
        printf("bitmap_alloc_block: block number out of range\n");
        return 0;
    }
    
    return block_num;
}

// 释放一个数据块
void bitmap_free_block(uint32 block_num)
{
    // 检查块号是否有效
    if (block_num < sb.data_start || 
        block_num >= sb.data_start + sb.data_blocks) {
        panic("bitmap_free_block: invalid block number");
    }
    
    // 计算在位图中的位置
    uint32 bit_num = block_num - sb.data_start;
    bitmap_unset(sb.data_bitmap_start, bit_num);
}

// 分配一个inode
// 返回inode号，失败返回0xFFFF
uint16 bitmap_alloc_inode(void)
{
    uint32 bit_num = bitmap_search_and_set(sb.inode_bitmap_start);
    
    if (bit_num == 0xFFFFFFFF) {
        printf("bitmap_alloc_inode: no free inodes\n");
        return 0xFFFF;
    }
    
    // 检查是否超出范围
    uint32 max_inodes = INODE_PER_BLOCK * sb.inode_blocks;
    if (bit_num >= max_inodes) {
        printf("bitmap_alloc_inode: inode number out of range\n");
        return 0xFFFF;
    }
    
    return (uint16)bit_num;
}

// 释放一个inode
void bitmap_free_inode(uint16 inode_num)
{
    // 检查inode号是否有效
    uint32 max_inodes = INODE_PER_BLOCK * sb.inode_blocks;
    if (inode_num >= max_inodes) {
        panic("bitmap_free_inode: invalid inode number");
    }
    
    bitmap_unset(sb.inode_bitmap_start, inode_num);
}

// 打印所有已经分配出去的bit序号
void bitmap_print(uint32 bitmap_block_num)
{
    uint8 bit_cmp;
    uint32 byte, shift;

    printf("\nbitmap:\n");

    buf_t* buf = buf_read(bitmap_block_num);
    for(byte = 0; byte < BLOCK_SIZE; byte++) {
        bit_cmp = 1;
        for(shift = 0; shift <= 7; shift++) {
            if(bit_cmp & buf->data[byte])
               printf("bit %d is alloced\n", byte * 8 + shift);
            bit_cmp = bit_cmp << 1;
        }
    }
    printf("over\n");
    buf_release(buf);
}
