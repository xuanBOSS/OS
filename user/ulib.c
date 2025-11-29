// user/ulib.c
#include "user.h"

// 全局错误变量
int errno = 0;

//字符串函数实现
uint32 strlen(const char *s) {
    uint32 n;
    for (n = 0; s[n]; n++);
    return n;
}

char* strcpy(char *s, const char *t) {
    char *os = s;
    while ((*s++ = *t++) != 0);
    return os;
}

int strcmp(const char *p, const char *q) {
    while (*p && *p == *q)
        p++, q++;
    return (uint8)*p - (uint8)*q;
}

char* strchr(const char *s, char c) {
    for (; *s; s++)
        if (*s == c)
            return (char*)s;
    return NULL;
}

//内存函数实现
void* memset(void *dst, int c, uint32 n) {
    char *cdst = (char*)dst;
    uint32 i;
    for (i = 0; i < n; i++) {
        cdst[i] = c;
    }
    return dst;
}

void* memcpy(void *dst, const void *src, uint32 n) {
    return memmove(dst, src, n);
}

void* memmove(void *vdst, const void *vsrc, int n) {
    char *dst = vdst;
    const char *src = vsrc;
    
    if (src > dst) {
        while (n-- > 0)
            *dst++ = *src++;
    } else {
        dst += n;
        src += n;
        while (n-- > 0)
            *--dst = *--src;
    }
    return vdst;
}

int memcmp(const void *s1, const void *s2, uint32 n) {
    const char *p1 = s1, *p2 = s2;
    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

//输入输出函数
int puts(const char *s) {
    int len = strlen(s);
    int ret1 = write(STDOUT_FILENO, s, len);
    int ret2 = write(STDOUT_FILENO, "\n", 1);
    return (ret1 >= 0 && ret2 >= 0) ? len + 1 : -1;
}

int putchar(int c) {
    char ch = c;
    return write(STDOUT_FILENO, &ch, 1);
}

char* gets(char *buf, int max) {
    int i, cc;
    char c;
    
    for (i = 0; i + 1 < max;) {
        cc = read(STDIN_FILENO, &c, 1);
        if (cc < 1)
            break;
        buf[i++] = c;
        if (c == '\n' || c == '\r')
            break;
    }
    buf[i] = '\0';
    return buf;
}

int atoi(const char *s) {
    int n = 0;
    while ('0' <= *s && *s <= '9')
        n = n * 10 + *s++ - '0';
    return n;
}
