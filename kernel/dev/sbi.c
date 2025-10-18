#include "common.h"
#include "riscv.h"
#include "lib/print.h"
#include "proc/proc.h"

// SBI调用号定义
#define SBI_SET_TIMER 0x0
#define SBI_CONSOLE_PUTCHAR 0x1
#define SBI_CONSOLE_GETCHAR 0x2
#define SBI_CLEAR_IPI 0x3
#define SBI_SEND_IPI 0x4
#define SBI_REMOTE_FENCE_I 0x5
#define SBI_REMOTE_SFENCE_VMA 0x6
#define SBI_REMOTE_SFENCE_VMA_ASID 0x7
#define SBI_SHUTDOWN 0x8

// SBI错误码
#define SBI_SUCCESS 0
#define SBI_ERR_FAILED -1
#define SBI_ERR_NOT_SUPPORTED -2
#define SBI_ERR_INVALID_PARAM -3

// SBI调用的通用接口
static long sbi_call(long extension, long function, long arg0, long arg1, long arg2) {
    register long a0 asm("a0") = arg0;
    register long a1 asm("a1") = arg1;
    register long a2 asm("a2") = arg2;
    register long a6 asm("a6") = function;
    register long a7 asm("a7") = extension;
    register long ret asm("a0");
    
    asm volatile("ecall" 
                 : "=r"(ret) 
                 : "r"(a0), "r"(a1), "r"(a2), "r"(a6), "r"(a7) 
                 : "memory");
    return ret;
}

// 设置时钟中断
void sbi_set_timer(uint64 stime) {
    int cpuid = mycpuid();
    
    // 调试输出
    printf("CPU %d: sbi_set_timer called with time=%lu\n", cpuid, stime);
    
    // 调用SBI设置时钟
    long result = sbi_call(SBI_SET_TIMER, 0, stime, 0, 0);
    
    if (result == SBI_SUCCESS) {
        printf("CPU %d: sbi_set_timer SUCCESS\n", cpuid);
    } else {
        printf("CPU %d: sbi_set_timer FAILED with code %ld\n", cpuid, result);
    }
}

// 其他有用的SBI接口
void sbi_console_putchar(int ch) {
    sbi_call(SBI_CONSOLE_PUTCHAR, 0, ch, 0, 0);
}

int sbi_console_getchar(void) {
    return sbi_call(SBI_CONSOLE_GETCHAR, 0, 0, 0, 0);
}

void sbi_shutdown(void) {
    sbi_call(SBI_SHUTDOWN, 0, 0, 0, 0);
}
