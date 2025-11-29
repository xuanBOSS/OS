#ifndef __SYSNUM_H__
#define __SYSNUM_H__

#define SYS_brk          1
#define SYS_mmap         2
#define SYS_munmap       3
#define SYS_copyin       4
#define SYS_copyout      5
#define SYS_copyinstr    6

// 测试系统调用
#define SYS_test_basic      10
#define SYS_test_args       11
#define SYS_test_error      12
#define SYS_test_privilege  13
#define SYS_test_pointer    14
#define SYS_test_string     15

// 任务4：基础系统调用
// 进程控制类
#define SYS_fork         16
#define SYS_exit         17
#define SYS_wait         18
#define SYS_kill         19
#define SYS_getpid       20
#define SYS_yield        21  
#define SYS_pipe         22  
#define SYS_getppid      28  

// 文件操作类
#define SYS_open         23  
#define SYS_close        24
#define SYS_read         25
#define SYS_write        26

// 内存管理类
#define SYS_sbrk         27  

#define SYS_lseek    29
#define SYS_stat     30
#define SYS_fstat    31
#define SYS_mkdir    32
#define SYS_chdir    33
#define SYS_getcwd   34
#define SYS_link     35
#define SYS_unlink   36
#define SYS_dup      37
#define SYS_dup2     38
#define SYS_symlink  39  
#define SYS_readlink 40  

#endif
