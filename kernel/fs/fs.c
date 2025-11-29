#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/bitmap.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/file.h"
#include "mem/str.h"
#include "lib/print.h"
#include "fs/log.h"

// 超级块在内存的副本
super_block_t sb;

#define FS_MAGIC 0x12345678
#define SB_BLOCK_NUM 0

// // 输出super_block的信息
// static void sb_print(void)
// {
//     printf("\nsuper block information:\n");
//     printf("magic = %x\n", sb.magic);
//     printf("block size = %d\n", sb.block_size);
//     printf("inode blocks = %d\n", sb.inode_blocks);
//     printf("data blocks = %d\n", sb.data_blocks);
//     printf("total blocks = %d\n", sb.total_blocks);
//     printf("inode bitmap start = %d\n", sb.inode_bitmap_start);
//     printf("inode start = %d\n", sb.inode_start);
//     printf("data bitmap start = %d\n", sb.data_bitmap_start);
//     printf("data start = %d\n", sb.data_start);
//      printf("log start = %d\n", sb.log_start);          
//     printf("log blocks = %d\n", sb.log_blocks);        
// }

// 文件系统初始化
void fs_init(void)
{
    // 初始化块缓存（不打印）
    buf_init();
    
    // 读取超级块
    buf_t* buf = buf_read(SB_BLOCK_NUM);
    if (!buf) {
        panic("fs_init: failed to read superblock");
    }
    
    memmove(&sb, buf->data, sizeof(sb));
    buf_release(buf);
    
    // 验证
    assert(sb.magic == FS_MAGIC, "fs_init: magic");
    assert(sb.block_size == BLOCK_SIZE, "fs_init: block size");
    
    // 初始化其他组件（不打印）
    log_init();
    inode_init();
    file_init();
    dcache_init();
    
    // ✅ 只在最后打印一次
    printf("\n=== File System Initialized ===\n");
    printf("Magic: 0x%x\n", sb.magic);
    printf("Total blocks: %d\n", sb.total_blocks);
    printf("Inode blocks: %d\n", sb.inode_blocks);
    printf("================================\n");
}

// 同步所有脏块到磁盘
void fs_sync(void)
{
    printf("Syncing file system...\n");
    buf_sync_all();
    printf("File system synced\n");
}

// 卸载文件系统
void fs_unmount(void)
{
    printf("Unmounting file system...\n");
    
    // 同步所有脏块
    fs_sync();
    
    // TODO: 可以添加其他清理工作
    // - 关闭所有打开的文件
    // - 释放缓存
    // - 写回超级块（如果有修改）
    
    printf("File system unmounted\n");
}
