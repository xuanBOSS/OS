// user/test.c
#include "user.h"

int main(void);

void _start(void) {
    main();
    exit(0);  // 确保程序正确退出
}

int main() {
    printf("=== TASK 5 User System Call Interface Test ===\n");
    
    // 测试基本系统调用
    printf("Testing basic system calls...\n");
    int pid = getpid();
    printf("My PID: %d\n", pid);
    
    // 测试字符串函数
    printf("Testing string functions...\n");
    char buf[100];
    strcpy(buf, "Hello World");
    printf("String: %s (length: %d)\n", buf, strlen(buf));
    
    if (strcmp(buf, "Hello World") == 0) {
        printf("String comparison works!\n");
    }
    
// 测试内存分配
printf("Testing memory allocation...\n");
printf("DEBUG: Testing sbrk directly first...\n");

// 直接测试 sbrk
void* old_brk = sbrk(0);
printf("DEBUG: current heap: %p\n", old_brk);

void* new_brk = sbrk(4096);
printf("DEBUG: sbrk(4096) returned: %p\n", new_brk);

if (new_brk == (void*)-1) {
    printf("DEBUG: sbrk failed! Cannot allocate memory\n");
} else {
    printf("DEBUG: sbrk succeeded, now testing malloc...\n");
    char *ptr = malloc(100);
    printf("DEBUG: malloc returned: %p\n", ptr);
    
    if (ptr) {
        strcpy(ptr, "Malloc works!");
        printf("Allocated memory: %s\n", ptr);
        free(ptr);
        printf("Memory freed\n");
    } else {
        printf("DEBUG: malloc failed even after sbrk succeeded\n");
    }
}
    
    // 测试文件操作
    printf("Testing file operations...\n");
    int fd = open("/dev/console", O_WRONLY);
    if (fd >= 0) {
        write(fd, "File write test\n", 16);
        close(fd);
        printf("File operations work!\n");
    } else {
        printf("File open failed\n");
    }
    
    // 测试进程控制
    printf("Testing process control...\n");
    int child_pid = fork();
    if (child_pid == 0) {
        printf("Child process (PID: %d) running\n", getpid());
        exit(42);
    } else if (child_pid > 0) {
        printf("Parent waiting for child %d\n", child_pid);
        int status;
        wait(&status);
        printf("Child finished with status: %d\n", status);
    } else {
        printf("Fork failed\n");
    }
    
    printf("=== All tests completed! ===\n");
    exit(0);
}