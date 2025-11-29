#include "mem/str.h"

void* memset(void* dst,int c,uint64 n)
{
    char* cdst=(char*)dst;
    for(uint64 i=0;i<n;i++)
    {
        cdst[i]=c;
    }
    return dst;
}

void* memcpy(void* dst,const void* src,uint64 n)
{
    char* cdst=(char*)dst;
    const char* csrc=(const char*)src;
    for(uint64 i=0;i<n;i++)
    {
        cdst[i]=csrc[i];
    }
    return dst;
}

void* memmove(void* dst, const void* src, uint64 n)
{
    char* cdst = (char*)dst;
    const char* csrc = (const char*)src;
    
    // 检查内存区域是否重叠
    if (cdst < csrc) {
        // 目标在源之前，从前往后复制
        for (uint64 i = 0; i < n; i++) {
            cdst[i] = csrc[i];
        }
    } else if (cdst > csrc) {
        // 目标在源之后，从后往前复制（避免覆盖）
        for (uint64 i = n; i > 0; i--) {
            cdst[i - 1] = csrc[i - 1];
        }
    }
    // 如果 dst == src，不需要复制
    
    return dst;
}

int memcmp(const void* s1,const void* s2,uint64 n)
{
    const char* c1=(const char*)s1;
    const char* c2=(const char*)s2;
    for(uint64 i=0;i<n;i++)
    {
        if(c1[i]!=c2[i])
        {
            return c1[i]-c2[i];
        }
    }
    return 0;
}

// ✅ 添加 strlen 函数
uint64 strlen(const char* s)
{
    uint64 len = 0;
    while(s[len] != '\0')
    {
        len++;
    }
    return len;
}

// ✅ 添加其他常用字符串函数
char* strcpy(char* dst, const char* src)
{
    char* original_dst = dst;
    while((*dst++ = *src++) != '\0')
        ;
    return original_dst;
}

char* strncpy(char* dst, const char* src, uint64 n)
{
    char* original_dst = dst;
    uint64 i;
    
    for(i = 0; i < n && src[i] != '\0'; i++)
    {
        dst[i] = src[i];
    }
    
    // 如果源字符串长度小于n，用'\0'填充剩余部分
    for(; i < n; i++)
    {
        dst[i] = '\0';
    }
    
    return original_dst;
}

int strcmp(const char* s1, const char* s2)
{
    while(*s1 && (*s1 == *s2))
    {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, uint64 n)
{
    for(uint64 i = 0; i < n; i++)
    {
        if(s1[i] != s2[i])
        {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
        if(s1[i] == '\0')
        {
            return 0;
        }
    }
    return 0;
}

char* strchr(const char* s, int c)
{
    while(*s != '\0')
    {
        if(*s == c)
        {
            return (char*)s;
        }
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

char* strrchr(const char* s, int c)
{
    const char* last = NULL;
    while(*s != '\0')
    {
        if(*s == c)
        {
            last = s;
        }
        s++;
    }
    return (c == '\0') ? (char*)s : (char*)last;
}

char* safestrcpy(char* dst, const char* src, int n) {
    char* os = dst;
    if (n <= 0) return os;
    while (--n > 0 && (*dst++ = *src++) != 0)
        ;
    *dst = 0;
    return os;
}

char* strncat(char* dst, const char* src, uint64 n)
{
    char* original_dst = dst;
    
    // 找到dst的末尾
    while (*dst != '\0') {
        dst++;
    }
    
    // 拼接最多n个字符
    uint64 i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    
    // 添加终止符
    dst[i] = '\0';
    
    return original_dst;
}

char* strcat(char* dst, const char* src)
{
    char* original_dst = dst;
    
    // 找到dst的末尾
    while (*dst != '\0') {
        dst++;
    }
    
    // 复制src到dst末尾
    while ((*dst++ = *src++) != '\0')
        ;
    
    return original_dst;
}
