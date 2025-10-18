#ifndef __SBI_H__
#define __SBI_H__

#include "common.h"

// SBI接口声明
void sbi_set_timer(uint64 stime);
void sbi_console_putchar(int ch);
int sbi_console_getchar(void);
void sbi_shutdown(void);

#endif
