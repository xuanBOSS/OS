// user/initcode.c
#include "sys.h"

void _start(void) {
    // 测试基本的 fork + exit + wait
    
    // 测试 fork
    int pid = fork();
    
    if (pid == 0) {
        // 子进程
        exit(42);  // 子进程退出，退出码为42
    } else if (pid > 0) {
        // 父进程
        int status;
        wait(&status);  // 等待子进程退出
        exit(0);        // 父进程正常退出
    } else {
        // fork 失败
        exit(-1);
    }
}
