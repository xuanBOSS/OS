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

