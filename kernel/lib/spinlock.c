#include "lib/lock.h"
#include "lib/print.h"
#include "proc/proc.h"
#include "proc/cpu.h"
#include "riscv.h"

// 带层数叠加的关中断
void push_off(void)
{
    cpu_t *c = mycpu();
    int old = intr_get();

    intr_off();

    if(c->noff == 0)
        c->origin = old;
    c->noff += 1;
}

// 带层数叠加的开中断
void pop_off(void)
{
    cpu_t *c = mycpu();
    
    if(c->noff < 1)
        panic("pop_off");
    
    c->noff -= 1;
    if(c->noff == 0 && c->origin)
        intr_on();
}

// 是否持有自旋锁
// 中断应当是关闭的
bool spinlock_holding(spinlock_t *lk)
{
    int r;
    push_off();
    r = (lk->locked && lk->cpuid == mycpuid());
    pop_off();
    return r;
}

// 自选锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{
    lk->name = name;
    lk->locked = 0;
    lk->cpuid = -1;
}

// 获取自选锁
void spinlock_acquire(spinlock_t *lk)
{    
    push_off();
    
    // 原子交换，获取锁
    while(__sync_lock_test_and_set(&lk->locked, 1) != 0) {
        asm volatile("nop");
    }
    
    __sync_synchronize();
    lk->cpuid = mycpuid();
} 

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
    
    lk->cpuid = -1;
    
    // 内存屏障，确保临界区代码不会被重排到锁释放之后
    __sync_synchronize();
    
    // 释放锁
    __sync_lock_release(&lk->locked);
    
    pop_off(); // 开中断
}

