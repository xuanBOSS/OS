#ifndef __FCNTL_H__
#define __FCNTL_H__

// 文件访问模式
#define O_RDONLY    0x000
#define O_WRONLY    0x001
#define O_RDWR      0x002

// 文件创建标志
#define O_CREATE    0x200
#define O_EXCL      0x800

// 文件状态标志
#define O_TRUNC     0x400
#define O_APPEND    0x008

// 其他标志
#define O_NONBLOCK  0x004

#endif
