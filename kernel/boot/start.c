#include "riscv.h"

__attribute__ ((aligned (4096))) uint8 CPU_stack[4096 * NCPU];

void start()
{
    // 允许所有CPU执行到main函数
    extern int main();
    main();
}
