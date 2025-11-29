#ifndef __SYSFUNC_H__
#define __SYSFUNC_H__

#include "common.h"

uint64 sys_brk();
uint64 sys_mmap();
uint64 sys_munmap();
uint64 sys_copyin();
uint64 sys_copyout();
uint64 sys_copyinstr();

// 进程控制类
uint64 sys_fork(void);
uint64 sys_exit(void);
uint64 sys_wait(void);
uint64 sys_kill(void);
uint64 sys_getpid(void);
uint64 sys_yield(void);

// 文件操作类
uint64 sys_open(void);
uint64 sys_close(void);
uint64 sys_read(void);
uint64 sys_write(void);
uint64 sys_lseek(void);     // 文件定位
uint64 sys_stat(void);      // 获取文件信息
uint64 sys_fstat(void);     // 获取打开文件信息
uint64 sys_mkdir(void);     // 创建目录
uint64 sys_chdir(void);     // 切换目录
uint64 sys_getcwd(void);    // 获取当前目录
uint64 sys_link(void);      // 硬链接
uint64 sys_unlink(void);    // 删除文件
uint64 sys_dup(void);       // 复制文件描述符
uint64 sys_dup2(void);      // 复制文件描述符到指定位置
uint64 sys_symlink(void);   // 符号链接（可选）
uint64 sys_readlink(void);  // 读取符号链接（可选）

// 内存管理类
uint64 sys_sbrk(void);

uint64 sys_pipe(void);

#endif
