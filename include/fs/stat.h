#ifndef __STAT_H__
#define __STAT_H__

#include "common.h"

#define T_DIR     1   // Directory
#define T_FILE    2   // File
#define T_DEVICE  3   // Device

struct stat {
    int dev;        // File system's disk device
    uint32 ino;     // Inode number
    int16 type;     // Type of file
    int16 nlink;    // Number of links to file
    uint64 size;    // Size of file in bytes
};

#endif
