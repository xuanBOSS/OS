#ifndef __STR_H__
#define __STR_H__

#include "common.h"

void* memset(void* dst, int c, uint64 n);
void* memcpy(void* dst, const void* src, uint64 n);
int   memcmp(const void* s1, const void* s2, uint64 n);

uint64 strlen(const char* s);
char*  strcpy(char* dst, const char* src);
char*  strncpy(char* dst, const char* src, uint64 n);
int    strcmp(const char* s1, const char* s2);
int    strncmp(const char* s1, const char* s2, uint64 n);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);

#endif
