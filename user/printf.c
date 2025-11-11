// user/printf.c
#include "user.h"

static void putc(int fd, char c) {
    write(fd, &c, 1);
}

static void printint(int fd, int xx, int base, int sign) {
    static char digits[] = "0123456789abcdef";
    char buf[16];
    int i;
    uint32 x;
    
    if (sign && (sign = xx < 0))
        x = -xx;
    else
        x = xx;
    
    i = 0;
    do {
        buf[i++] = digits[x % base];
    } while ((x /= base) != 0);
    
    if (sign)
        buf[i++] = '-';
    
    while (--i >= 0)
        putc(fd, buf[i]);
}

static void printptr(int fd, uint64 x) {
    int i;
    putc(fd, '0');
    putc(fd, 'x');
    for (i = 0; i < (sizeof(uint64) * 2); i++, x <<= 4)
        putc(fd, "0123456789abcdef"[x >> (sizeof(uint64) * 8 - 4)]);
}

// ✅ 使用标准 va_list
void vprintf(int fd, const char *fmt, va_list ap) {
    char *s;
    int c, i, state;
    
    if (!fmt) return;  // 防止空指针
    
    state = 0;
    for (i = 0; fmt[i] && i < 1000; i++) {  // 限制最大长度，防止无限循环
        c = fmt[i] & 0xff;
        if (state == 0) {
            if (c == '%') {
                state = '%';
            } else {
                putc(fd, c);
            }
        } else if (state == '%') {
            if (c == 'd') {
                printint(fd, va_arg(ap, int), 10, 1);
            } else if (c == 'x') {
                printint(fd, va_arg(ap, int), 16, 0);
            } else if (c == 'p') {
                printptr(fd, va_arg(ap, uint64));
            } else if (c == 's') {
                s = va_arg(ap, char*);
                if (s == NULL) {
                    s = "(null)";
                }
                // 限制字符串长度，防止无限循环
                for (int j = 0; *s && j < 256; s++, j++) {
                    putc(fd, *s);
                }
            } else if (c == 'c') {
                putc(fd, va_arg(ap, int));
            } else if (c == '%') {
                putc(fd, c);
            } else {
                // 未知格式符
                putc(fd, '%');
                putc(fd, c);
            }
            state = 0;
        }
    }
}

// ✅ 修复：正确使用 va_list
void fprintf(int fd, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fd, fmt, ap);
    va_end(ap);
}

void printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(1, fmt, ap);  // 直接使用 fd=1，避免 STDOUT_FILENO 可能的问题
    va_end(ap);
}
