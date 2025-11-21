#include "common.h"
#include "sys.h"

#define NUM_ITEMS 20

void _start(void);  // 前置声明

// ========================================
// 主函数（必须在最前面）
// ========================================
void _start(void) {
    int pipefd[2];
    
    // 创建管道
    if (pipe(pipefd) < 0) {
        exit(-1);
    }
    
    int read_fd = pipefd[0];
    int write_fd = pipefd[1];
    
    // 创建生产者进程
    int pid1 = fork();
    if (pid1 == 0) {
        // 子进程1：生产者
        close(read_fd);
        
        // 写入 0-19 到管道
        for (int i = 0; i < NUM_ITEMS; i++) {
            write(write_fd, &i, sizeof(i));
            
            // 模拟生产时间
            for (volatile int j = 0; j < 10000; j++);
            
            // 偶尔让出 CPU
            if (i % 5 == 0) {
                yield();
            }
        }
        
        close(write_fd);
        exit(0);  // 生产者退出码 0
        
    } else if (pid1 < 0) {
        exit(-2);
    }
    
    // 创建消费者进程
    int pid2 = fork();
    if (pid2 == 0) {
        // 子进程2：消费者
        close(write_fd);
        
        int data;
        int count = 0;
        int sum = 0;
        
        // 从管道读取数据
        while (read(read_fd, &data, sizeof(data)) == sizeof(data)) {
            sum += data;
            count++;
            
            // 模拟消费时间
            for (volatile int j = 0; j < 10000; j++);
            
            // 偶尔让出 CPU
            if (count % 5 == 0) {
                yield();
            }
        }
        
        close(read_fd);
        
        // 验证：count 应该是 20，sum 应该是 190 (0+1+...+19)
        // 返回 count 作为退出码
        exit(count);
        
    } else if (pid2 < 0) {
        exit(-3);
    }
    
    // 父进程：关闭两端并等待子进程
    close(read_fd);
    close(write_fd);
    
    // 等待两个子进程
    int status1, status2;
    wait(&status1);  // 等待第一个结束的子进程
    wait(&status2);  // 等待第二个结束的子进程
    
    // 其中一个是生产者（返回 0），另一个是消费者（返回 20）
    int consumer_count = (status1 > status2) ? status1 : status2;
    
    // 返回消费者读取的数量（期望 20）
    exit(consumer_count);
}