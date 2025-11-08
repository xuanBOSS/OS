// user/initcode.c
#include "sys.h"

void _start(void) {
    // 最简单的死循环，不调用任何系统调用
    while(1) {
        // 纯粹的死循环
        for (volatile int i = 0; i < 1000; i++);
    }
}
