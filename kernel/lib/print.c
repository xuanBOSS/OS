// 标准输出和报错机制
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"

volatile int panicked = 0;

static spinlock_t print_lk;

static char digits[] = "0123456789abcdef";

void print_init(void)
{
    //spinlock_init(&print_lk, "print");
    uart_init(); // 初始化串口
    // 手动初始化锁（简单赋值，避免函数调用）
    print_lk.locked = 0;
    print_lk.name = "print";
    print_lk.cpuid = -1;
    // 确保 UART 初始化完成（延迟）
    for (volatile int i = 0; i < 100000; i++) {
        asm volatile("nop");
    }
}

static void printint(int xx, int base, int sign)
{
    char buf[16];
    int i;
    uint32 x;

    if(sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;

    i = 0;
    do{
        buf[i++] = digits[x % base];
    }while((x /= base) != 0);

    if(sign)
        buf[i++] = '-';

    while(--i >= 0)
        uart_putc_sync(buf[i]);
}

static void printptr(uint64 x)
{
    int i;
    uart_putc_sync('0');
    uart_putc_sync('x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
        uart_putc_sync(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void printf(const char *fmt, ...)
{
    va_list ap;
    int i, c;
    char *s;

    if (fmt == 0)
        return;

    spinlock_acquire(&print_lk);  // 获取锁

    va_start(ap, fmt);
    for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
        if(c != '%'){
            uart_putc_sync(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if(c == 0)
            break;
        switch(c){
        case 'd':
            printint(va_arg(ap, int), 10, 1);
            break;
        case 'x':
            printint(va_arg(ap, int), 16, 1);
            break;
        case 'p':
            printptr(va_arg(ap, uint64));
            break;
        case 's':
            if((s = va_arg(ap, char*)) == 0)
                s = "(null)";
            for(; *s; s++)
                uart_putc_sync(*s);
            break;
        case '%':
            uart_putc_sync('%');
            break;
        default:
            uart_putc_sync('%');
            uart_putc_sync(c);
            break;
        }
    }
    va_end(ap);

    spinlock_release(&print_lk);  // 释放锁
}

void panic(const char *s)
{
    printf("panic: ");
    printf("%s\n", s);
    panicked = 1; // 冻结其他CPU
    for(;;)
        ;
}

void assert(bool condition, const char* warning)
{
    if (!condition) {
        panic(warning);
    }
}

