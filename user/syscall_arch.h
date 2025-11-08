#ifndef __SYSCALL_ARCH_H__
#define __SYSCALL_ARCH_H__

static inline long __syscall0(long n)
{
    register long a7 asm("a7") = n;
    register long a0 asm("a0");
    asm volatile (
        "ecall"
        : "=r" (a0)
        : "r" (a7)
        : "memory"
    );
    return a0;
}

static inline long __syscall1(long n, long a)
{
    register long a7 asm("a7") = n;
    register long a0 asm("a0") = a;
    asm volatile (
        "ecall"
        : "+r" (a0)
        : "r" (a7)
        : "memory"
    );
    return a0;
}

static inline long __syscall2(long n, long a, long b)
{
    register long a7 asm("a7") = n;
    register long a0 asm("a0") = a;
    register long a1 asm("a1") = b;
    asm volatile (
        "ecall"
        : "+r" (a0)
        : "r" (a7), "r" (a1)
        : "memory"
    );
    return a0;
}

static inline long __syscall3(long n, long a, long b, long c)
{
    register long a7 asm("a7") = n;
    register long a0 asm("a0") = a;
    register long a1 asm("a1") = b;
    register long a2 asm("a2") = c;
    asm volatile (
        "ecall"
        : "+r" (a0)
        : "r" (a7), "r" (a1), "r" (a2)
        : "memory"
    );
    return a0;
}

static inline long __syscall4(long n, long a, long b, long c, long d)
{
    register long a7 asm("a7") = n;
    register long a0 asm("a0") = a;
    register long a1 asm("a1") = b;
    register long a2 asm("a2") = c;
    register long a3 asm("a3") = d;
    asm volatile (
        "ecall"
        : "+r" (a0)
        : "r" (a7), "r" (a1), "r" (a2), "r" (a3)
        : "memory"
    );
    return a0;
}

static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
    register long a7 asm("a7") = n;
    register long a0 asm("a0") = a;
    register long a1 asm("a1") = b;
    register long a2 asm("a2") = c;
    register long a3 asm("a3") = d;
    register long a4 asm("a4") = e;
    asm volatile (
        "ecall"
        : "+r" (a0)
        : "r" (a7), "r" (a1), "r" (a2), "r" (a3), "r" (a4)
        : "memory"
    );
    return a0;
}

static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
    register long a7 asm("a7") = n;
    register long a0 asm("a0") = a;
    register long a1 asm("a1") = b;
    register long a2 asm("a2") = c;
    register long a3 asm("a3") = d;
    register long a4 asm("a4") = e;
    register long a5 asm("a5") = f;
    asm volatile (
        "ecall"
        : "+r" (a0)
        : "r" (a7), "r" (a1), "r" (a2), "r" (a3), "r" (a4), "r" (a5)
        : "memory"
    );
    return a0;
}

#define VDSO_USEFUL
#define IPC_64 0

#endif
