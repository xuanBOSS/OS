// 这个头文件通常认为其他.h文件都应该include
#ifndef __COMMON_H__
#define __COMMON_H__
#include <stdbool.h>
// 类型定义

typedef char                   int8;
typedef short                  int16;
typedef int                    int32;
typedef long long              int64;
typedef unsigned char          uint8; 
typedef unsigned short         uint16;
typedef unsigned int           uint32;
typedef unsigned long long     uint64;

typedef unsigned long long         reg; 
//typedef enum {false = 0, true = 1} bool;

#ifndef NULL
#define NULL ((void*)0)
#endif

#define NCPU 3  
//单核 1
//双核 2

// 新增：页面大小定义
#define PGSIZE 4096             // 页面大小：4KB
#define PGSHIFT 12              // 页面位移：2^12 = 4096

// 页面对齐宏
#define PGROUNDUP(sz)   (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a)  (((a)) & ~(PGSIZE-1))

// 内核页面数量定义（可根据需要调整）
#define KERNEL_PAGES 1024       // 内核保留1024个页面（4MB）

#endif
