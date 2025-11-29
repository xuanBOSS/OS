#ifndef __SYSCALL_H__
#define __SYSCALL_H__

#include "syscall_arch.h"
#include "syscall_num.h"

#ifndef __scc
#define __scc(X) ((long)(X))
typedef long syscall_arg_t;
#endif

#define __syscall1(n, a) __syscall1(n, __scc(a))
#define __syscall2(n, a, b) __syscall2(n, __scc(a), __scc(b))
#define __syscall3(n, a, b, c) __syscall3(n, __scc(a), __scc(b), __scc(c))
#define __syscall4(n, a, b, c, d) __syscall4(n, __scc(a), __scc(b), __scc(c), __scc(d))
#define __syscall5(n, a, b, c, d, e) __syscall5(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e))
#define __syscall6(n, a, b, c, d, e, f) __syscall6(n, __scc(a), __scc(b), __scc(c), __scc(d), __scc(e), __scc(f))

#define __SYSCALL_NARGS_X(a, b, c, d, e, f, g, h, n, ...) n
#define __SYSCALL_NARGS(...) __SYSCALL_NARGS_X(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_CONCAT_X(a, b) a##b
#define __SYSCALL_CONCAT(a, b) __SYSCALL_CONCAT_X(a, b)
#define __SYSCALL_DISP(b, ...)                        \
    __SYSCALL_CONCAT(b, __SYSCALL_NARGS(__VA_ARGS__)) \
    (__VA_ARGS__)

#define __syscall(...) __SYSCALL_DISP(__syscall, __VA_ARGS__)
#define syscall(...) __syscall(__VA_ARGS__)

typedef int pid_t;

// ================================
// 进程控制类
// ================================
extern int fork(void);
extern int exit(int) __attribute__((noreturn));
extern int wait(int* status);
extern int getpid(void);
extern int getppid(void);
extern void yield(void);

// ================================
// 文件操作类
// ================================
extern int pipe(int*);
extern int open(const char*, int);
extern int close(int);
extern int read(int fd, void* buf, int count);
extern int write(int fd, const void* buf, int count);

// ✅ 新增：扩展的文件操作
extern int lseek(int fd, int offset, int whence);
extern int dup(int oldfd);
extern int dup2(int oldfd, int newfd);

// ================================
// 文件信息类
// ================================
struct stat;  // 前向声明

extern int stat(const char* path, struct stat* statbuf);
extern int fstat(int fd, struct stat* statbuf);

// ================================
// 目录操作类
// ================================
extern int mkdir(const char* path, int mode);
extern int chdir(const char* path);
extern char* getcwd(char* buf, int size);

// ================================
// 链接操作类
// ================================
extern int link(const char* oldpath, const char* newpath);
extern int unlink(const char* path);

// ✅ 可选：符号链接
extern int symlink(const char* target, const char* linkpath);
extern int readlink(const char* path, char* buf, int size);

// ================================
// 内存管理类
// ================================
extern char* sbrk(int increment);

#endif // __SYSCALL_H__
