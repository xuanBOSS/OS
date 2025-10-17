// print.c - 完整版本
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"

volatile int panicked = 0;

static spinlock_t print_lk;

static char digits[] = "0123456789abcdef";

void print_init(void)
{
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

// 打印32位整数（有符号）
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

// 打印32位无符号整数
static void printuint(uint32 x, int base)
{
    char buf[16];
    int i;

    i = 0;
    do{
        buf[i++] = digits[x % base];
    }while((x /= base) != 0);

    while(--i >= 0)
        uart_putc_sync(buf[i]);
}

// 打印64位整数（有符号）
static void printint64(int64 xx, int base, int sign)
{
    char buf[32];
    int i;
    uint64 x;

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

// 打印64位无符号整数
static void printuint64(uint64 x, int base)
{
    char buf[32];
    int i;

    i = 0;
    do{
        buf[i++] = digits[x % base];
    }while((x /= base) != 0);

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

// 增强版printf，支持 %d, %u, %x, %c, %s, %p, %ld, %lu, %lx
void printf(const char *fmt, ...)
{
    va_list ap;
    int i, c;
    char *s;

    if (fmt == 0)
        return;

    spinlock_acquire(&print_lk);

    va_start(ap, fmt);
    for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
        if(c != '%'){
            uart_putc_sync(c);
            continue;
        }
        c = fmt[++i] & 0xff;
        if(c == 0)
            break;
        
        // 检查长度修饰符
        int is_long = 0;
        if(c == 'l') {
            is_long = 1;
            c = fmt[++i] & 0xff;
            if(c == 'l') {  // 支持 %ll
                c = fmt[++i] & 0xff;
            }
        }
        
        switch(c){
        case 'd':
            if(is_long) {
                printint64(va_arg(ap, int64), 10, 1);
            } else {
                printint(va_arg(ap, int), 10, 1);
            }
            break;
        case 'u':
            if(is_long) {
                printuint64(va_arg(ap, uint64), 10);
            } else {
                printuint(va_arg(ap, uint32), 10);
            }
            break;
        case 'x':
            if(is_long) {
                printuint64(va_arg(ap, uint64), 16);
            } else {
                printuint(va_arg(ap, uint32), 16);
            }
            break;
        case 'c':
            uart_putc_sync(va_arg(ap, int));
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
            // 打印未知格式
            uart_putc_sync('%');
            if(is_long) uart_putc_sync('l');
            uart_putc_sync(c);
            break;
        }
    }
    va_end(ap);

    spinlock_release(&print_lk);
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
