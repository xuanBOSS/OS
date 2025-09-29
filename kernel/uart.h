/* kernel/uart.h */
#ifndef _UART_H_
#define _UART_H_

/* 新函数 */
void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int uart_getc(void);
void uart_puthex(unsigned long x);

/* 兼容原版xv6的函数 */
void uartinit(void);           /* 调用uart_init */
void uartputc_sync(int c);     /* 调用uart_putc */
void uartwrite(char buf[], int n);
void uartintr(void);

/* UART寄存器定义 */
#define UART0_BASE      0x10000000L
#define UART_THR        0
#define UART_RHR        0
#define UART_IER        1
#define UART_FCR        2
#define UART_LCR        3
#define UART_MCR        4
#define UART_LSR        5

#define LSR_RX_READY    (1<<0)
#define LSR_TX_IDLE     (1<<5)
#define LCR_EIGHT_BITS  (3<<0)
#define LCR_BAUD_LATCH  (1<<7)
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR  (3<<1)

/* 兼容宏定义 */
#define uartinit() uart_init()
#endif

