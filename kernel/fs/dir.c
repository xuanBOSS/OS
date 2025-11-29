#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/inode.h"
#include "fs/dir.h"
#include "fs/log.h"   
#include "fs/bitmap.h"
#include "mem/str.h"
#include "lib/print.h"
#include "proc/cpu.h"

// 对目录文件的简化性假设: 每个目录文件只包括一个block
// 也就是每个目录下最多 BLOCK_SIZE / sizeof(dirent_t) = 32 个目录项

/*----------------------- 哈希函数 -------------------------*/

// 计算字符串哈希值
uint16 hash_string(const char* s)
{
    uint16 h = 0;
    while (*s) {
        h = (h << 5) + h + (uint8)*s++;  // h = h * 33 + c
    }
    return h;
}

/*----------------------- 目录缓存 (dcache) -------------------------*/

#define DCACHE_SIZE 16

typedef struct {
    uint16 parent_inum;           // 父目录 inode 号
    char name[DIR_NAME_LEN];      // 文件名
    uint16 child_inum;            // 子 inode 号
    bool valid;                   // 是否有效
} dcache_entry_t;

static dcache_entry_t dcache[DCACHE_SIZE];
static spinlock_t dcache_lock;
static int dcache_next = 0;  // FIFO 替换指针

// 初始化目录缓存
void dcache_init(void)
{
    spinlock_init(&dcache_lock, "dcache");
    for (int i = 0; i < DCACHE_SIZE; i++) {
        dcache[i].valid = false;
    }
    printf("dcache: initialized with %d entries\n", DCACHE_SIZE);
}

// 查询缓存
// 返回 child_inum，如果未找到返回 INODE_NUM_UNUSED
uint16 dcache_lookup(uint16 parent_inum, const char* name)
{
    spinlock_acquire(&dcache_lock);
    
    for (int i = 0; i < DCACHE_SIZE; i++) {
        if (dcache[i].valid &&
            dcache[i].parent_inum == parent_inum &&
            strncmp(dcache[i].name, name, DIR_NAME_LEN) == 0) {
            uint16 inum = dcache[i].child_inum;
            spinlock_release(&dcache_lock);
            return inum;
        }
    }
    
    spinlock_release(&dcache_lock);
    return INODE_NUM_UNUSED;
}

// 添加到缓存
void dcache_add(uint16 parent_inum, const char* name, uint16 child_inum)
{
    spinlock_acquire(&dcache_lock);
    
    // 检查是否已存在（避免重复添加）
    for (int i = 0; i < DCACHE_SIZE; i++) {
        if (dcache[i].valid &&
            dcache[i].parent_inum == parent_inum &&
            strncmp(dcache[i].name, name, DIR_NAME_LEN) == 0) {
            // 已存在，只更新 child_inum
            dcache[i].child_inum = child_inum;
            spinlock_release(&dcache_lock);
            return;
        }
    }
    
    // FIFO 替换策略
    dcache[dcache_next].parent_inum = parent_inum;
    strncpy(dcache[dcache_next].name, name, DIR_NAME_LEN);
    dcache[dcache_next].name[DIR_NAME_LEN - 1] = '\0';
    dcache[dcache_next].child_inum = child_inum;
    dcache[dcache_next].valid = true;
    
    dcache_next = (dcache_next + 1) % DCACHE_SIZE;
    
    spinlock_release(&dcache_lock);
}

// 使缓存失效（删除文件时调用）
void dcache_invalidate(uint16 parent_inum, const char* name)
{
    spinlock_acquire(&dcache_lock);
    
    for (int i = 0; i < DCACHE_SIZE; i++) {
        if (dcache[i].valid &&
            dcache[i].parent_inum == parent_inum &&
            strncmp(dcache[i].name, name, DIR_NAME_LEN) == 0) {
            dcache[i].valid = false;
            break;
        }
    }
    
    spinlock_release(&dcache_lock);
}

/*----------------------- 符号链接支持 -------------------------*/

// 解析符号链接
// depth 防止循环链接
inode_t* resolve_symlink(inode_t* ip, int depth)
{
    if (ip == NULL) {
        return NULL;
    }
    
    if (depth > MAX_SYMLINK_DEPTH) {
        printf("resolve_symlink: symlink depth exceeded\n");
        return NULL;
    }
    
    if (ip->type != FT_SYMLINK) {
        return ip;  // 不是符号链接，直接返回
    }
    
    // 读取目标路径
    char target[DIR_PATH_LEN];
    uint32 len = ip->size < DIR_PATH_LEN ? ip->size : DIR_PATH_LEN - 1;
    inode_read_data(ip, 0, len, target, false);
    target[len] = '\0';
    
    // 释放当前 inode
    inode_t* target_ip = path_to_inode(target);
    
    // 递归解析
    return resolve_symlink(target_ip, depth + 1);
}

/*----------------------- 目录操作 -------------------------*/

// 查询一个目录项是否在目录里
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_search_entry(inode_t *pip, char *name)
{
    assert(sleeplock_holding(&pip->slk), "dir_search_entry: lock");
    
    printf("dir_search_entry: searching for '%s' in inode %d\n", name, pip->inode_num);
    
    if (pip->type != FT_DIR) {
        panic("dir_search_entry: not a directory");
    }

    printf("dir_search_entry: checking cache\n");
    uint16 cached_inum = dcache_lookup(pip->inode_num, name);
    if (cached_inum != INODE_NUM_UNUSED) {
        // 缓存命中
        printf("dir_search_entry: cache hit, returning %d\n", cached_inum);
        return cached_inum;
    }
    
    // 先计算哈希值
    uint16 target_hash = hash_string(name);
    
    printf("dir_search_entry: searching for '%s' in inode %d (size=%d, hash=%d)\n", 
           name, pip->inode_num, pip->size, target_hash);
    
    dirent_t de;
    uint32 iter_count = 0;
    for (uint32 offset = 0; offset < pip->size; offset += sizeof(dirent_t)) {
        iter_count++;
        if (iter_count > 100) {
            printf("dir_search_entry: WARNING - too many iterations (%d), breaking\n", iter_count);
            break;
        }
        
        printf("dir_search_entry: reading at offset %d (iter=%d)\n", offset, iter_count);
        uint32 n = inode_read_data(pip, offset, sizeof(de), &de, false);
        printf("dir_search_entry: read %d bytes\n", n);
        
        if (n != sizeof(de)) {
            // printf("dir_search_entry: read failed or incomplete, breaking\n");
            break;
        }
        
        // printf("dir_search_entry: entry inum=%d, hash=%d, name='%s'\n", 
        //        de.inode_num, de.hash, de.name);
        
        // 先比较哈希，再比较字符串
        if (de.inode_num != INODE_NUM_UNUSED &&
            de.hash == target_hash &&
            strncmp(de.name, name, DIR_NAME_LEN) == 0) {
            // printf("dir_search_entry: found match!\n");
            dcache_add(pip->inode_num, name, de.inode_num);    
            return de.inode_num;
        }
    }
    
    // printf("dir_search_entry: not found after %d iterations\n", iter_count);
    
    return INODE_NUM_UNUSED;
}

// 在pip目录下添加一个目录项
// 成功返回这个目录项的偏移量 (同时更新pip->size)
// 失败返回BLOCK_SIZE (没有空间 或 发生重名)
// ps: 调用者需持有pip的锁
uint32 dir_add_entry(inode_t *pip, uint16 inode_num, char *name)
{
    assert(sleeplock_holding(&pip->slk), "dir_add_entry: lock");
    
    if (pip->type != FT_DIR) {
        panic("dir_add_entry: not a directory");
    }
    
    // 检查名字长度
    uint32 name_len = strlen(name);
    if (name_len >= DIR_NAME_LEN) {
        printf("dir_add_entry: name too long\n");
        return BLOCK_SIZE;
    }
    
    // 检查是否重名
    if (dir_search_entry(pip, name) != INODE_NUM_UNUSED) {
        printf("dir_add_entry: name already exists\n");
        return BLOCK_SIZE;
    }
    
    // 寻找空闲目录项
    dirent_t de;
    uint32 offset;
    for (offset = 0; offset < BLOCK_SIZE; offset += sizeof(dirent_t)) {
        if (offset >= pip->size) {
            // 超出当前大小，可以添加
            break;
        }
        
        if (inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
            break;
        }
        
        if (de.inode_num == INODE_NUM_UNUSED) {
            // 找到空闲槽位
            break;
        }
    }
    
    // 检查目录是否已满
    if (offset >= BLOCK_SIZE) {
        printf("dir_add_entry: directory full\n");
        return BLOCK_SIZE;
    }
    
    // 创建新目录项
    de.inode_num = inode_num;
    de.hash = hash_string(name);  // 计算哈希值
    strncpy(de.name, name, DIR_NAME_LEN);
    de.name[DIR_NAME_LEN - 1] = '\0';
    
    // 写入目录项
    printf("dir_add_entry: calling inode_write_data, offset=%d, name='%s'\n", offset, name);
    int write_ret = inode_write_data(pip, offset, sizeof(de), &de, false);
    printf("dir_add_entry: inode_write_data returned %d\n", write_ret);
    if (write_ret != sizeof(de)) {
        printf("dir_add_entry: write failed\n");
        return BLOCK_SIZE;
    }
    
    // 更新目录大小
    if (offset + sizeof(de) > pip->size) {
        printf("dir_add_entry: updating directory size from %d to %d\n", pip->size, offset + sizeof(de));
        pip->size = offset + sizeof(de);
        printf("dir_add_entry: calling inode_rw to write size\n");
        inode_rw(pip, true);
        printf("dir_add_entry: inode_rw completed\n");
    }
    
    printf("dir_add_entry: calling dcache_add\n");
    dcache_add(pip->inode_num, name, inode_num);
    printf("dir_add_entry: dcache_add completed\n");

    printf("dir_add_entry: returning offset=%d\n", offset);
    return offset;
}

// 在pip目录下删除一个目录项
// 成功返回这个目录项的inode_num
// 失败返回INODE_NUM_UNUSED
// ps: 调用者需持有pip的锁
uint16 dir_delete_entry(inode_t *pip, char *name)
{
    assert(sleeplock_holding(&pip->slk), "dir_delete_entry: lock");
    
    if (pip->type != FT_DIR) {
        panic("dir_delete_entry: not a directory");
    }
    
    // 使用哈希优化查找
    uint16 target_hash = hash_string(name);
    
    dirent_t de;
    for (uint32 offset = 0; offset < pip->size; offset += sizeof(dirent_t)) {
        if (inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
            break;
        }
        
        if (de.inode_num != INODE_NUM_UNUSED &&
            de.hash == target_hash &&
            strncmp(de.name, name, DIR_NAME_LEN) == 0) {
            // 找到目标，清除目录项
            uint16 inode_num = de.inode_num;
            de.inode_num = INODE_NUM_UNUSED;
            de.hash = 0;
            memset(de.name, 0, DIR_NAME_LEN);
            
            inode_write_data(pip, offset, sizeof(de), &de, false);

            dcache_invalidate(pip->inode_num, name);

            return inode_num;
        }
    }
    
    return INODE_NUM_UNUSED;
}

// 把目录下的有效目录项复制到dst (dst区域长度为len)
// 返回读到的字节数 (sizeof(dirent_t)*n)
// 调用者需要持有pip的锁
uint32 dir_get_entries(inode_t* pip, uint32 len, void* dst, bool user)
{
    assert(sleeplock_holding(&pip->slk), "dir_get_entries: lock");
    
    if (pip->type != FT_DIR) {
        panic("dir_get_entries: not a directory");
    }
    
    uint32 copied = 0;
    dirent_t de;
    
    for (uint32 offset = 0; offset < pip->size && copied < len; offset += sizeof(dirent_t)) {
        if (inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
            break;
        }
        
        if (de.inode_num != INODE_NUM_UNUSED) {
            // 复制有效目录项
            if (user) {
                uvm_copyout(myproc()->pgtbl, (uint64)dst + copied, (uint64)&de, sizeof(de));
            } else {
                memmove((char*)dst + copied, &de, sizeof(de));
            }
            copied += sizeof(de);
        }
    }
    
    return copied;
}

// 改变进程里存储的当前目录
// 成功返回0 失败返回-1
uint32 dir_change(char* path)
{
    inode_t* ip = path_to_inode(path);
    if (ip == NULL) {
        return -1;
    }
    
    inode_lock(ip);
    if (ip->type != FT_DIR) {
        inode_unlock_free(ip);
        return -1;
    }
    inode_unlock(ip);
    
    // 释放旧的当前目录
    inode_free(myproc()->cwd);
    
    // 设置新的当前目录
    myproc()->cwd = ip;
    
    return 0;
}

// 输出一个目录下的所有有效目录项
// for debug
// ps: 调用者需持有pip的锁
void dir_print(inode_t *pip)
{
    assert(sleeplock_holding(&pip->slk), "dir_print: lock");

    printf("\ninode_num = %d dirents:\n", pip->inode_num);

    dirent_t de;
    for (uint32 offset = 0; offset < pip->size; offset += sizeof(dirent_t))
    {
        if (inode_read_data(pip, offset, sizeof(de), &de, false) != sizeof(de)) {
            break;
        }
        
        if (de.inode_num != INODE_NUM_UNUSED) {
            printf("inum = %d hash = 0x%x name = %s\n", 
                   de.inode_num, de.hash, de.name);
        }
    }
}

/*----------------------- 路径(一串目录和文件) -------------------------*/

// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
static char *skip_element(char *path, char *name)
{
    while(*path == '/') path++;
    if(*path == 0) return 0;

    char *s = path;
    while (*path != '/' && *path != 0)
        path++;

    int len = path - s;
    if (len >= DIR_NAME_LEN) {
        memmove(name, s, DIR_NAME_LEN);
    } else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

// 查找路径path对应的inode (find_parent = false)
// 查找路径path对应的inode的父节点 (find_parent = true)
// 供两个上层函数使用
// 失败返回NULL
static inode_t* search_inode(char* path, char* name, bool find_parent)
{
    inode_t* ip;
    inode_t* next;
    
    // printf("search_inode: path='%s', find_parent=%d\n", path, find_parent);
    
    // 确定起点
    if (*path == '/') {
        ip = inode_alloc(INODE_ROOT);
        printf("search_inode: allocated root inode\n");
    } else {
        printf("search_inode: duplicating cwd inode (cwd=%p)\n", myproc()->cwd);
        if (myproc()->cwd == NULL) {
            panic("search_inode: cwd is NULL");
        }
        ip = inode_dup(myproc()->cwd);
        printf("search_inode: duplicated cwd inode, ip->inode_num=%d\n", ip->inode_num);
    }
    
    printf("search_inode: entering while loop, path='%s'\n", path);
    char* remaining;
    while ((remaining = skip_element(path, name)) != 0) {
        printf("search_inode: skip_element returned '%s', name='%s'\n", remaining, name);
        // 检查是否还有剩余路径
        if (*remaining == '\0') {
            // 这是最后一个元素
            printf("search_inode: last element='%s', find_parent=%d\n", name, find_parent);
            printf("search_inode: calling inode_lock on inode %d\n", ip->inode_num);
            inode_lock(ip);
            printf("search_inode: inode_lock returned\n");
            // printf("search_inode: inode_lock returned\n");
            
            if (ip->type != FT_DIR) {
                inode_unlock_free(ip);
                return NULL;
            }
            
            // 如果找父目录，返回当前目录
            if (find_parent) {
                // printf("search_inode: found parent directory, returning\n");
                inode_unlock(ip);
                return ip;
            }
            
            // 查找最后一个元素
            // printf("search_inode: calling dir_search_entry for '%s'\n", name);
            uint16 next_inum = dir_search_entry(ip, name);
            // printf("search_inode: dir_search_entry returned inum=%d\n", next_inum);
            if (next_inum == INODE_NUM_UNUSED) {
                inode_unlock_free(ip);
                return NULL;
            }
            
            next = inode_alloc(next_inum);
            inode_unlock_free(ip);
            ip = next;
            break;
        }
        
        // 还有更多路径，继续遍历
        // printf("search_inode: element='%s', remaining='%s'\n", name, path);
        path = remaining;
        // printf("search_inode: calling inode_lock in loop on inode %d\n", ip->inode_num);
        inode_lock(ip);
        // printf("search_inode: inode_lock in loop returned\n");
        
        if (ip->type != FT_DIR) {
            inode_unlock_free(ip);
            return NULL;
        }
        
        // 查找下一级
        // printf("search_inode: calling dir_search_entry for '%s'\n", name);
        uint16 next_inum = dir_search_entry(ip, name);
        // printf("search_inode: dir_search_entry returned inum=%d\n", next_inum);
        if (next_inum == INODE_NUM_UNUSED) {
            inode_unlock_free(ip);
            return NULL;
        }
        
        next = inode_alloc(next_inum);
        inode_unlock_free(ip);
        ip = next;
    }
    
    if (find_parent) {
        inode_free(ip);
        return NULL;
    }
    
    return ip;
}

// 找到path对应的inode
inode_t* path_to_inode(char* path)
{
    char name[DIR_NAME_LEN];
    inode_t* ip = search_inode(path, name, false);
    
    // 解析符号链接
    if (ip != NULL) {
        inode_lock(ip);
        if (ip->type == FT_SYMLINK) {
            inode_unlock(ip);
            return resolve_symlink(ip, 0);
        }
        inode_unlock(ip);
    }
    
    return ip;
}

// 找到path对应的inode的父节点
// path最后的目录名放入name指向的空间
inode_t* path_to_pinode(char* path, char* name)
{
    return search_inode(path, name, true);
}

// 如果path对应的inode存在则返回inode
// 如果path对应的inode不存在则创建inode
// 失败返回NULL
inode_t* path_create_inode(char* path, uint16 type, uint16 major, uint16 minor)
{
    char name[DIR_NAME_LEN];

    printf("path_create_inode: path='%s', type=%d\n", path, type);
    // ✅ 移除 begin_op()，由调用者管理事务（sys_mkdir 已经调用了 begin_op()）

    // 查找父目录
    printf("path_create_inode: calling path_to_pinode\n");
    inode_t* dp = path_to_pinode(path, name);
    printf("path_create_inode: path_to_pinode returned %p, name='%s'\n", dp, name);
    if (dp == NULL) {
        printf("path_create_inode: path_to_pinode failed\n");
        // ✅ 移除 end_op()，由调用者管理事务
        return NULL;
    }
    
    printf("path_create_inode: calling inode_lock on dp\n");
    inode_lock(dp);
    printf("path_create_inode: inode_lock returned\n");
    
    // 检查是否已存在
    printf("path_create_inode: checking if '%s' already exists\n", name);
    uint16 inum = dir_search_entry(dp, name);
    printf("path_create_inode: dir_search_entry returned inum=%d\n", inum);
    if (inum != INODE_NUM_UNUSED) {
        // 已存在，返回已有的 inode
        inode_unlock(dp);
        inode_free(dp);
        
        // ✅ 关键：分配 inode 后需要检查它的有效性
        inode_t* ip = inode_alloc(inum);
        
        // ✅ 加锁以确保 inode 数据从磁盘加载
        inode_lock(ip);
        
        // ✅ 如果 type 是 unused，说明 inode 已被删除
        if (ip->type == FT_UNUSED) {
            inode_unlock(ip);
            inode_free(ip);
            end_op();
            return NULL;
        }
        
        // ✅ 解锁后返回（调用者会再次加锁）
        inode_unlock(ip);
        // ✅ 移除 end_op()，由调用者管理事务
        return ip;
    }
    
    // ✅ 文件不存在，需要创建
    printf("path_create_inode: file does not exist, creating new inode\n");
    
    // 创建新 inode
    printf("path_create_inode: calling inode_create\n");
    inode_t* ip = inode_create(type, major, minor);
    if (ip == NULL) {
        printf("path_create_inode: inode_create failed\n");
        inode_unlock(dp);
        inode_free(dp);
        // ✅ 移除 end_op()，由调用者管理事务
        return NULL;
    }
    printf("path_create_inode: inode_create returned inode %d\n", ip->inode_num);
    
    inode_lock(ip);
    
    // 添加目录项
    printf("path_create_inode: calling dir_add_entry for '%s'\n", name);
    int ret = dir_add_entry(dp, ip->inode_num, name);
    printf("path_create_inode: dir_add_entry returned %d\n", ret);
    if (ret == BLOCK_SIZE) {  // ✅ 修改：检查返回值是否小于 0
        // 添加失败，清理
        ip->nlink = 0;  // ✅ 标记为可删除
        ip->type = FT_UNUSED;
        inode_rw(ip, true);
        inode_unlock(ip);
        inode_free(ip);
        inode_unlock(dp);
        inode_free(dp);
        // ✅ 移除 end_op()，由调用者管理事务
        return NULL;
    }
    
    // 如果是目录，添加 . 和 ..
    if (type == FT_DIR) {
        printf("path_create_inode: adding '.' entry, ip->inode_num=%d\n", ip->inode_num);
        printf("path_create_inode: checking ip lock before dir_add_entry\n");
        // ✅ 确保 ip 持有锁（应该已经持有，从第 626 行）
        int ret_dot = dir_add_entry(ip, ip->inode_num, ".");
        printf("path_create_inode: '.' entry added, ret=%d\n", ret_dot);
        printf("path_create_inode: adding '..' entry, dp->inode_num=%d\n", dp->inode_num);
        int ret_dotdot = dir_add_entry(ip, dp->inode_num, "..");
        printf("path_create_inode: '..' entry added, ret=%d\n", ret_dotdot);
        
        // ✅ 父目录的 nlink++（因为子目录的 ".." 指向父目录）
        dp->nlink++;
        inode_rw(dp, true);
    }
    
    printf("path_create_inode: file created successfully, inode=%d\n", ip->inode_num);
    
    inode_rw(ip, true);
    inode_unlock(ip);
    inode_unlock(dp);
    inode_free(dp);
    
    // ✅ 移除 end_op()，由调用者管理事务（sys_mkdir 会调用 end_op()）
    printf("path_create_inode: completed, returning inode\n");
    
    return ip;
}

// 文件链接(目录不能被链接)
// 本质是创建一个目录项, 这个目录项的inode_num是存在的而不用申请
// 成功返回0 失败返回-1
uint32 path_link(char* old_path, char* new_path)
{
    char name[DIR_NAME_LEN];

    begin_op();
    
    // 获取旧文件的 inode
    inode_t* ip = path_to_inode(old_path);
    if (ip == NULL) {
        end_op(); 
        return -1;
    }
    
    inode_lock(ip);
    
    // 不能链接目录
    if (ip->type == FT_DIR) {
        inode_unlock_free(ip);
        end_op(); 
        return -1;
    }
    
    // 增加链接计数
    ip->nlink++;
    inode_rw(ip, true);
    inode_unlock(ip);
    
    // 在新路径创建目录项
    inode_t* dp = path_to_pinode(new_path, name);
    if (dp == NULL) {
        inode_lock(ip);
        ip->nlink--;
        inode_rw(ip, true);
        inode_unlock_free(ip);
        end_op(); 
        return -1;
    }
    
    inode_lock(dp);
    if (dir_add_entry(dp, ip->inode_num, name) == BLOCK_SIZE) {
        inode_unlock_free(dp);
        inode_lock(ip);
        ip->nlink--;
        inode_rw(ip, true);
        inode_unlock_free(ip);
        end_op(); 
        return -1;
    }
    
    inode_unlock_free(dp);
    inode_free(ip);

    end_op(); 
    
    return 0;
}

// 检查一个unlink操作是否合理
// 调用者需要持有ip的锁
// 在path_unlink()中调用
static bool check_unlink(inode_t* ip)
{
    assert(sleeplock_holding(&ip->slk), "check_unlink: slk");

    // 如果不是目录，可以删除
    if (ip->type != FT_DIR) {
        return true;
    }

    // 目录必须为空（只有 . 和 ..）
    uint8 tmp[sizeof(dirent_t) * 3];
    uint32 read_len = dir_get_entries(ip, sizeof(tmp), tmp, false);
    
    if(read_len == sizeof(dirent_t) * 3) {
        return false;  // 目录不为空
    } else if(read_len == sizeof(dirent_t) * 2) {
        return true;   // 目录只有 . 和 ..
    } else {
        panic("check_unlink: unexpected read_len");
        return false;
    }
}

// 文件删除链接
uint32 path_unlink(char* path)
{
    char name[DIR_NAME_LEN];

    begin_op(); 
    
    // 获取父目录
    inode_t* dp = path_to_pinode(path, name);
    if (dp == NULL) {
        end_op(); 
        return -1;
    }
    
    inode_lock(dp);
    
    // 不能删除 . 和 ..
    if (strncmp(name, ".", DIR_NAME_LEN) == 0 || 
        strncmp(name, "..", DIR_NAME_LEN) == 0) {
        inode_unlock_free(dp);
        end_op(); 
        return -1;
    }
    
    // 查找目标 inode
    uint16 inum = dir_search_entry(dp, name);
    if (inum == INODE_NUM_UNUSED) {
        inode_unlock_free(dp);
        end_op(); 
        return -1;
    }
    
    inode_t* ip = inode_alloc(inum);
    inode_lock(ip);
    
    // 检查是否可以删除
    if (!check_unlink(ip)) {
        inode_unlock_free(ip);
        inode_unlock_free(dp);
        end_op(); 
        return -1;
    }
    
    // 删除目录项
    dir_delete_entry(dp, name);
    
    // 如果是目录，减少父目录的链接计数
    if (ip->type == FT_DIR) {
        dp->nlink--;
        inode_rw(dp, true);
    }
    
    inode_unlock_free(dp);
    
    // 减少链接计数
    ip->nlink--;
    inode_rw(ip, true);
    inode_unlock_free(ip);

    end_op(); 
    
    return 0;
}

/*----------------------- inode 到路径的转换 -------------------------*/

// 递归构建从 inode 到根的路径（前向声明）
static char* inode_to_path_recursive(inode_t* ip, char* buf, int size);

// 将 inode 转换为路径
char* inode_to_path(inode_t* ip, char* buf, int size)
{
    if (!ip || !buf || size < 2) {
        return NULL;
    }
    
    // 情况1：根目录
    if (ip->inode_num == INODE_ROOT) {
        if (size < 2) return NULL;
        buf[0] = '/';
        buf[1] = '\0';
        return buf;
    }
    
    // 使用递归方式构建完整路径
    return inode_to_path_recursive(ip, buf, size);
}

// 递归构建从 inode 到根的路径
static char* inode_to_path_recursive(inode_t* ip, char* buf, int size)
{
    printf("inode_to_path_recursive: ip->inode_num=%d\n", ip ? ip->inode_num : -1);
    
    if (!ip || !buf || size < 2) {
        printf("inode_to_path_recursive: invalid arguments\n");
        return NULL;
    }
    
    // 基础情况：根目录
    if (ip->inode_num == INODE_ROOT) {
        printf("inode_to_path_recursive: root directory\n");
        buf[0] = '/';
        buf[1] = '\0';
        return buf;
    }
    
    printf("inode_to_path_recursive: locking inode %d\n", ip->inode_num);
    inode_lock(ip);
    
    // 查找父目录（通过 ".." 目录项）
    printf("inode_to_path_recursive: searching for '..' entry\n");
    uint16 parent_inum = dir_search_entry(ip, "..");
    printf("inode_to_path_recursive: parent_inum=%d\n", parent_inum);
    if (parent_inum == INODE_NUM_UNUSED) {
        printf("inode_to_path_recursive: parent not found\n");
        inode_unlock(ip);
        return NULL;
    }
    
    printf("inode_to_path_recursive: allocating parent inode %d\n", parent_inum);
    inode_t* parent = inode_alloc(parent_inum);
    inode_unlock(ip);
    
    // 递归构建父目录路径
    printf("inode_to_path_recursive: recursively building parent path\n");
    char parent_path[DIR_PATH_LEN];
    if (inode_to_path_recursive(parent, parent_path, sizeof(parent_path)) == NULL) {
        printf("inode_to_path_recursive: failed to build parent path\n");
        inode_free(parent);
        return NULL;
    }
    printf("inode_to_path_recursive: parent path='%s'\n", parent_path);
    
    // 在父目录中查找当前 inode 的名字
    printf("inode_to_path_recursive: searching for current inode %d in parent\n", ip->inode_num);
    inode_lock(parent);
    char name[DIR_NAME_LEN] = {0};
    bool found = false;
    
    dirent_t de;
    printf("inode_to_path_recursive: parent->size=%d\n", parent->size);
    for (uint32 offset = 0; offset < parent->size; offset += sizeof(dirent_t)) {
        printf("inode_to_path_recursive: reading entry at offset=%d\n", offset);
        if (inode_read_data(parent, offset, sizeof(de), &de, false) != sizeof(de)) {
            printf("inode_to_path_recursive: failed to read entry\n");
            break;
        }
        
        printf("inode_to_path_recursive: entry inum=%d, name='%s'\n", de.inode_num, de.name);
        if (de.inode_num == ip->inode_num) {
            strncpy(name, de.name, DIR_NAME_LEN);
            found = true;
            printf("inode_to_path_recursive: found name='%s'\n", name);
            break;
        }
    }
    
    inode_unlock(parent);
    inode_free(parent);
    
    if (!found) {
        return NULL;
    }
    
    // 拼接路径
    int parent_len = strlen(parent_path);
    int name_len = strlen(name);
    
    if (parent_len + 1 + name_len + 1 > size) {
        return NULL;  // 缓冲区不足
    }
    
    // 拼接：parent_path + "/" + name
    strncpy(buf, parent_path, size);
    if (parent_len > 1) {  // 如果不是根目录，添加 "/"
        buf[parent_len] = '/';
        buf[parent_len + 1] = '\0';
    }
    strncat(buf, name, size - strlen(buf) - 1);
    
    return buf;
}

/*----------------------- 符号链接创建 -------------------------*/

// 创建符号链接
// target: 目标路径（符号链接指向的文件）
// linkpath: 符号链接的路径
inode_t* path_create_symlink(const char* target, const char* linkpath)
{
    // 检查目标路径长度
    uint32 target_len = strlen(target);
    if (target_len == 0 || target_len >= DIR_PATH_LEN) {
        printf("path_create_symlink: invalid target path length\n");
        return NULL;
    }
    
    //begin_op();
    
    // 1. 创建符号链接 inode
    inode_t* ip = path_create_inode((char*)linkpath, FT_SYMLINK, 0, 0);
    if (!ip) {
        //end_op();
        printf("path_create_symlink: failed to create symlink inode\n");
        return NULL;
    }

    begin_op();
    
    inode_lock(ip);
    
    // 2. 将目标路径写入 inode 的数据块
    uint32 written = inode_write_data(ip, 0, target_len, (void*)target, false);
    if (written != target_len) {
        // 写入失败，清理
        ip->type = FT_UNUSED;
        ip->nlink = 0;
        inode_rw(ip, true);
        inode_unlock(ip);
        inode_free(ip);
        end_op();
        printf("path_create_symlink: failed to write target path\n");
        return NULL;
    }
    
    // 3. 更新 inode 元数据
    ip->size = target_len;
    inode_rw(ip, true);
    
    inode_unlock(ip);
    end_op();
    
    printf("path_create_symlink: created '%s' -> '%s'\n", linkpath, target);
    return ip;
}
