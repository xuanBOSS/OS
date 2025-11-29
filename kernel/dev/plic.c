// Platform-level interrupt controller

#include "memlayout.h"
#include "dev/plic.h"
#include "proc/proc.h"
#include "proc/cpu.h"
#include "lib/print.h"

// PLIC初始化
void plic_init()
{
    // 使能 IRQ 1-10 的所有中断（广撒网）
    *(uint32*)(PLIC_PRIORITY(1)) = 1;
    *(uint32*)(PLIC_PRIORITY(2)) = 1;
    *(uint32*)(PLIC_PRIORITY(3)) = 1;
    *(uint32*)(PLIC_PRIORITY(4)) = 1;
    *(uint32*)(PLIC_PRIORITY(5)) = 1;
    *(uint32*)(PLIC_PRIORITY(6)) = 1;
    *(uint32*)(PLIC_PRIORITY(7)) = 1;
    *(uint32*)(PLIC_PRIORITY(8)) = 1;
    *(uint32*)(PLIC_PRIORITY(10)) = 1;
    
    // 使能所有中断（位 1-10）
    *(uint32*)(PLIC_SENABLE(0)) = 0x7FE;  // 二进制: 0111 1111 1110
    
    // 阈值为 0
    *(uint32*)(PLIC_SPRIORITY(0)) = 0;
    
    printf("PLIC: Enabled IRQ 1-10, threshold=0\n");
}

// PLIC核心初始化
void plic_inithart()
{   
    int hartid = mycpuid();
    // 使能中断开关
    *(uint32*)PLIC_SENABLE(hartid) = (1 << UART_IRQ) | (1 << VIRTIO_IRQ);
    // 设置响应阈值
    *(uint32*)PLIC_SPRIORITY(hartid) = 0;
}

// 获取中断号
int plic_claim(void)
{
    int hartid = mycpuid();
    int irq = *(uint32*)PLIC_SCLAIM(hartid);
    return irq;
}

// 确认该中断号对应中断已经完成
void plic_complete(int irq)
{
    int hartid = mycpuid();
    *(uint32*)PLIC_SCLAIM(hartid) = irq;
}
