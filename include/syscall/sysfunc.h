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

// 文件操作类
uint64 sys_open(void);
uint64 sys_close(void);
uint64 sys_read(void);
uint64 sys_write(void);

// 内存管理类
uint64 sys_sbrk(void);

#endif
