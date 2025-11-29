#ifndef __DIR_H__
#define __DIR_H__

#include "common.h"

#define DIR_NAME_LEN 30  // 最大目录名长度
#define DIR_PATH_LEN 128 // 最大文件路径长度

// 添加哈希字段优化查找性能
typedef struct dirent {
    uint16 inode_num;
    uint16 hash;          // ← 新增：名字的哈希值
    char name[DIR_NAME_LEN];
} dirent_t;

typedef struct inode inode_t;

// 目录操作
uint16 dir_search_entry(inode_t* pip, char* name);
uint32 dir_add_entry(inode_t* pip, uint16 inode_num, char* name);
uint16 dir_delete_entry(inode_t* pip, char* name);
uint32 dir_get_entries(inode_t* pip, uint32 len, void* dst, bool user);
uint32 dir_change(char* path);
void   dir_print(inode_t* pip);

// 路径解析
inode_t* path_to_inode(char* path);
inode_t* path_to_pinode(char* path, char* name);
inode_t* path_create_inode(char* path, uint16 type, uint16 major, uint16 minor);
uint32   path_link(char* old_path, char* new_path);
uint32   path_unlink(char* path);

// 路径构建
char* inode_to_path(inode_t* ip, char* buf, int size);

// 符号链接创建
inode_t* path_create_symlink(const char* target, const char* linkpath);

// 哈希函数
uint16 hash_string(const char* s);

// 符号链接支持
#define MAX_SYMLINK_DEPTH 8  // 最多跟踪8层符号链接
inode_t* resolve_symlink(inode_t* ip, int depth);

//目录缓存
void dcache_init(void);
uint16 dcache_lookup(uint16 parent_inum, const char* name);
void dcache_add(uint16 parent_inum, const char* name, uint16 child_inum);
void dcache_invalidate(uint16 parent_inum, const char* name);

#endif
