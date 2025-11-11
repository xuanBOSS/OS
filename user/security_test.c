#include "user.h"

void test_basic_syscalls(void) {
    printf("=== Testing Basic System Calls ===\n");
    
    // 测试 getpid
    printf("Testing getpid...\n");
    int pid = getpid();
    printf("Current PID: %d\n", pid);
    
    // 测试 fork
    printf("Testing fork...\n");
    int child_pid = fork();
    if (child_pid == 0) {
        // 子进程
        printf("Child process: PID=%d, Parent PID=%d\n", getpid(), getppid());
        exit(42);
    } else if (child_pid > 0) {
        // 父进程
        printf("Parent process: Child PID=%d\n", child_pid);
        int status;
        wait(&status);
        printf("Child exited with status: %d\n", status);
    } else {
        printf("Fork failed!\n");
    }
    
    printf("Basic syscalls test completed.\n\n");
}

void test_parameter_passing(void) {
    printf("=== Testing Parameter Passing ===\n");
    
    // 测试正常参数传递
    char buffer[] = "Hello, World!\n";
    printf("Testing normal write...\n");
    int bytes_written = write(1, buffer, strlen(buffer));
    printf("Wrote %d bytes\n", bytes_written);
    
    // 测试边界情况
    printf("Testing edge cases...\n");
    
    // 1. 无效文件描述符
    int result = write(-1, buffer, 10);
    printf("Invalid fd write result: %d (should be -1)\n", result);
    
    // 2. 空指针
    result = write(1, NULL, 10);
    printf("NULL pointer write result: %d (should be -1)\n", result);
    
    // 3. 负数长度
    result = write(1, buffer, -1);
    printf("Negative length write result: %d (should be -1)\n", result);
    
    // 4. 零长度
    result = write(1, buffer, 0);
    printf("Zero length write result: %d (should be 0)\n", result);
    
    printf("Parameter passing test completed.\n\n");
}

void test_security(void) {
    printf("=== Testing Security ===\n");
    
    // 测试无效指针访问
    printf("Testing invalid pointer access...\n");
    char *invalid_ptr = (char*)0x10000000;
    int result = write(1, invalid_ptr, 10);
    printf("Invalid pointer write result: %d (should be -1)\n", result);
    
    // 测试内核地址访问
    printf("Testing kernel address access...\n");
    char *kernel_ptr = (char*)0x80000000;
    result = write(1, kernel_ptr, 10);
    printf("Kernel address write result: %d (should be -1)\n", result);
    
    // 测试缓冲区边界
    printf("Testing buffer boundaries...\n");
    char small_buf[4];
    // 注意：这里不能真的读取1000字节到4字节缓冲区
    // 这只是测试系统调用的参数检查
    printf("Buffer boundary test: would test read overflow protection\n");
    
    // 测试权限检查
    printf("Testing permission checks...\n");
    int fd = open("/nonexistent/file", 0);
    printf("Open nonexistent file result: %d (should be -1)\n", fd);
    
    printf("Security test completed.\n\n");
}

// 简化的时间获取函数
uint64 get_cycles(void) {
    uint64 cycles;
    asm volatile("rdcycle %0" : "=r" (cycles));
    return cycles;
}

void test_syscall_performance(void) {
    printf("=== Testing System Call Performance ===\n");
    
    const int iterations = 1000;  // 减少迭代次数避免过长输出
    
    // 测试 getpid 性能
    printf("Testing getpid performance...\n");
    uint64 start_time = get_cycles();
    
    for (int i = 0; i < iterations; i++) {
        getpid();
    }
    
    uint64 end_time = get_cycles();
    uint64 total_cycles = end_time - start_time;
    
    printf("%d getpid() calls took %lu cycles\n", iterations, total_cycles);
    printf("Average cycles per call: %lu\n", total_cycles / iterations);
    
    // 测试 write 性能
    printf("Testing write performance...\n");
    char test_buf[1] = {'.'};
    
    start_time = get_cycles();
    for (int i = 0; i < iterations; i++) {
        write(1, test_buf, 1);
    }
    end_time = get_cycles();
    
    total_cycles = end_time - start_time;
    printf("%d write() calls took %lu cycles\n", iterations, total_cycles);
    printf("Average cycles per call: %lu\n", total_cycles / iterations);
    
    printf("Performance test completed.\n\n");
}

int main(void) {
    printf("=== System Call Comprehensive Test Suite ===\n");
    printf("Starting comprehensive system call testing...\n\n");
    
    // 运行所有测试
    test_basic_syscalls();
    test_parameter_passing();
    test_security();
    test_syscall_performance();
    
    printf("=== All Tests Completed ===\n");
    printf("System call implementation verification finished!\n");
    
    exit(0);
}
