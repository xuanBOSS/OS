
/* kernel/uart.c */
#include "uart.h"

#define REG(reg) ((volatile unsigned char *)(UART0_BASE + (reg)))
#define READ_REG(reg) (*(REG(reg)))
#define WRITE_REG(reg, v) (*(REG(reg)) = (v))

/* 初始化UART */
void uart_init(void)
{
    WRITE_REG(UART_IER, 0x00);
    WRITE_REG(UART_LCR, LCR_BAUD_LATCH);  
    WRITE_REG(0, 0x03);                   
    WRITE_REG(1, 0x00);                   
    WRITE_REG(UART_LCR, LCR_EIGHT_BITS);
    WRITE_REG(UART_FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);
}

/* 发送单个字符 */
void uart_putc(char c)
{
    while ((READ_REG(UART_LSR) & LSR_TX_IDLE) == 0)
        ;
    WRITE_REG(UART_THR, c);
}

/* 同步发送字符（兼容原版） */
void uartputc_sync(int c)
{
    uart_putc((char)c);
}

/* 发送字符串 */
void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s);
        s++;
    }
}

/* 发送字符串（兼容原版） */
void uartwrite(char buf[], int n)
{
    for (int i = 0; i < n; i++) {
        uart_putc(buf[i]);
    }
}

/* 接收字符（非阻塞） */
int uart_getc(void)
{
    if (READ_REG(UART_LSR) & LSR_RX_READY) {
        return READ_REG(UART_RHR);
    }
    return -1;
}

/* 中断处理（兼容原版） */
void uartintr(void)
{
    /* 简化版：不处理中断 */
}

/* 输出十六进制数 */
void uart_puthex(unsigned long x)
{
    uart_puts("0x");
    
    if (x == 0) {
        uart_putc('0');
        return;
    }
    
    /* 转换为16进制字符串 */
    char hex_chars[] = "0123456789abcdef";
    char buffer[17]; /* 64位地址最多16个字符 + '\0' */
    int i = 0;
    
    /* 从低位开始提取 */
    unsigned long temp = x;
    while (temp > 0) {
        buffer[i++] = hex_chars[temp & 0xF];
        temp >>= 4;
    }
    
    /* 反向输出 */
    while (i > 0) {
        uart_putc(buffer[--i]);
    }
}