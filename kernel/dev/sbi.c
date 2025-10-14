#include "common.h"
#include "riscv.h"

// 兼容接口，无实际实现
void sbi_set_timer(uint64 stime) {
    // 空实现，M-mode定时器负责真实定时
}