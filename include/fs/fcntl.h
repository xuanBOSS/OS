#ifndef __FCNTL_H__
#define __FCNTL_H__

// ================================
// 文件访问模式（互斥，只能选一个）
// ================================
#define O_RDONLY    0x000   // 只读
#define O_WRONLY    0x001   // 只写
#define O_RDWR      0x002   // 读写

// 访问模式掩码
#define O_ACCMODE   0x003   // 用于提取访问模式的掩码

// ================================
// 文件创建标志
// ================================
#define O_CREATE    0x200   // 文件不存在则创建（你已有）
#define O_CREAT     0x200   // 添加标准名称（与 O_CREATE 相同）
#define O_EXCL      0x800   // 与 O_CREAT 一起使用，文件存在则失败

// ================================
// 文件状态标志
// ================================
#define O_TRUNC     0x400   // 打开时截断文件
#define O_APPEND    0x008   // 追加模式

// ================================
// 其他标志
// ================================
#define O_NONBLOCK  0x004   // 非阻塞模式

// 目录相关标志
#define O_DIRECTORY 0x010   // 如果不是目录则失败
#define O_NOFOLLOW  0x020   // 不跟随符号链接

// lseek 的 whence 参数
#define SEEK_SET    0       // 相对文件开头
#define SEEK_CUR    1       // 相对当前位置
#define SEEK_END    2       // 相对文件结尾

// ================================
// 文件权限（用于 mkdir, open 等）
// ================================
#define S_IRWXU     0700    // 用户读写执行
#define S_IRUSR     0400    // 用户读
#define S_IWUSR     0200    // 用户写
#define S_IXUSR     0100    // 用户执行

#define S_IRWXG     0070    // 组读写执行
#define S_IRGRP     0040    // 组读
#define S_IWGRP     0020    // 组写
#define S_IXGRP     0010    // 组执行

#define S_IRWXO     0007    // 其他读写执行
#define S_IROTH     0004    // 其他读
#define S_IWOTH     0002    // 其他写
#define S_IXOTH     0001    // 其他执行

// 常用的权限组合
#define S_IRWXUGO   0777    // 所有人读写执行
#define S_IRUGO     0444    // 所有人只读
#define S_IWUGO     0222    // 所有人只写

#endif
