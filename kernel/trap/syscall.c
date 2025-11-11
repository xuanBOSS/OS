#include "lib/print.h"
#include "trap/trap.h"
#include "proc/proc.h"
#include "proc/cpu.h"
#include "syscall/syscall.h"
#include "syscall/syscall_table.h"
#include "mem/mmap.h"

void syscall_init(void) {
    printf("=== System Call Framework Initialization ===\n");
    
    // 初始化mmap管理器
    mmap_init();
    
    // 打印系统调用表信息
    printf("System call table size: %d\n", syscall_table_size);
    printf("Registered system calls:\n");
    
    for (int i = 0; i < syscall_table_size; i++) {
        if (syscall_table[i].func != NULL) {
            printf("  [%d] %s (args: %d, privilege: %d)\n", 
                   i, 
                   syscall_table[i].name ? syscall_table[i].name : "unnamed",
                   syscall_table[i].arg_count,
                   syscall_table[i].min_privilege);
        }
    }
    
    printf("System call interface initialized successfully!\n");
    printf("===============================================\n");
}
