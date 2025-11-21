// user/user.h
#ifndef _USER_H
#define _USER_H

#include "common.h"
#include "fs/fcntl.h"
#include "fs/stat.h"

typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)
#define va_copy(dest, src) __builtin_va_copy(dest, src)

//错误处理
extern int errno;
#define SBRK_ERROR ((char *)-1)

//标准文件描述符
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

// ================================
// 系统调用声明
// ================================

// 进程控制类
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int*);
int kill(int, int);
int getpid(void);
int getppid(void);
void yield(void);          

// 文件操作类
int pipe(int*);          
int open(const char*, int);
int close(int);
int read(int, void*, int);
int write(int, const void*, int);

// 内存管理类
char* sbrk(int);

// ================================
// 用户库函数声明
// ================================
char* strcpy(char*, const char*);
void* memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
char* gets(char*, int max);
uint32 strlen(const char*);
void* memset(void*, int, uint32);
int atoi(const char*);
int memcmp(const void*, const void*, uint32);
void* memcpy(void*, const void*, uint32);

// 格式化输出函数
void vprintf(int fd, const char *fmt, va_list ap);
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));
int puts(const char*);
int putchar(int);

// 内存分配函数
void* malloc(uint32);
void free(void*);

// 文件系统相关
int stat(const char*, struct stat*);
int fstat(int fd, struct stat*);

#endif
