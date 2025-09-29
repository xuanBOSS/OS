/* kernel/main.c - 符合实验要求的最小版本 */
#include "uart.h"

void main(void)
{
    /* 初始化UART */
    uart_init();
    
    /* 实验要求：输出 "Hello 05" */
    uart_puts("Hello OS!\n");
    
    /* 死循环 - 防止程序退出 */
    while (1) {
        /* 空循环，系统保持运行状态 */
    }
}