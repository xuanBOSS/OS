#ifndef __UART_H__
#define __UART_H__

#include "common.h"

void uart_init(void);
void uart_putc_sync(int c);
int  uart_getc_sync(void);
void uart_intr(void);

int uart_can_read(void);           // 检查是否有数据可读
void uart_intr_with_debug(void);   // 带调试信息的中断处理

#endif
