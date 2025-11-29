#ifndef __STAT_H__
#define __STAT_H__

#include "common.h"

#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device
#define T_SYMLINK 4  

struct stat {
    int dev;        // File system's disk device
    uint32 ino;     // Inode number
    int16 type;     // Type of file
    int16 nlink;    // Number of links to file
    uint64 size;    // Size of file in bytes
    uint32 atime;   // 访问时间
    uint32 mtime;   // 修改时间
    uint32 ctime;   // 创建时间
};

#endif
