#ifndef __STR_H__
#define __STR_H__

#include "common.h"

void* memset(void* dst, int c, uint64 n);
void* memcpy(void* dst, const void* src, uint64 n);
int   memcmp(const void* s1, const void* s2, uint64 n);

#endif
