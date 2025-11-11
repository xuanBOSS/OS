// kernel/syscall/syscall.c
#include "lib/print.h"
#include "proc/cpu.h"
#include "mem/mmap.h"
#include "mem/vmem.h"
#include "syscall/syscall.h"
#include "syscall/syscall_table.h"
#include "syscall/syscall_args.h"
#include "syscall/sysnum.h"
#include "syscall/sysfunc.h"

// 保持原有的简单分发器（向后兼容）
static uint64 (*syscalls[])(void) = {
    [SYS_brk]           sys_brk,
    [SYS_mmap]          sys_mmap,
    [SYS_munmap]        sys_munmap,
    [SYS_copyin]        sys_copyin,
    [SYS_copyout]       sys_copyout,
    [SYS_copyinstr]     sys_copyinstr,
    
    [16]                sys_fork,      // fork
    [17]                sys_exit,      // exit
    [18]                sys_wait,      // wait
    [19]                sys_kill,      // kill
    [20]                sys_getpid,    // getpid
    [21]                sys_open,      // open
    [22]                sys_close,     // close
    [23]                sys_read,      // read
    [24]                sys_write,     // write
    [25]                sys_sbrk,      // sbrk
};

// ✅ 修复：移除硬编码测试，使用正常的系统调用处理
void syscall()
{
    int num;
    proc_t* p = myproc();
    
    num = p->tf->a7;  // 系统调用号
    
    // ✅ 可选：简单的调试输出（可以注释掉）
    #ifdef SYSCALL_DEBUG
    printf("syscall: pid=%d, num=%d, args=(%ld,%ld,%ld)\n", 
           p->pid, num, p->tf->a0, p->tf->a1, p->tf->a2);
    #endif
    
    if (num > 0 && num < sizeof(syscalls)/sizeof(syscalls[0]) && syscalls[num]) {
        p->tf->a0 = syscalls[num]();  // 调用并保存返回值
        
        #ifdef SYSCALL_DEBUG
        printf("syscall: result=%ld\n", p->tf->a0);
        #endif
    } else {
        printf("Unknown syscall %d\n", num);
        p->tf->a0 = -1;
    }
}

// 参数读取函数（保持不变）
static uint64 arg_raw(int n)
{   
    proc_t* proc = myproc();
    switch(n) {
        case 0: return proc->tf->a0;
        case 1: return proc->tf->a1;
        case 2: return proc->tf->a2;
        case 3: return proc->tf->a3;
        case 4: return proc->tf->a4;
        case 5: return proc->tf->a5;
        default:
            panic("arg_raw: illegal arg num");
            return -1;
    }
}

void arg_uint32(int n, uint32* ip)
{
    *ip = arg_raw(n);
}

void arg_uint64(int n, uint64* ip)
{
    *ip = arg_raw(n);
}

void arg_str(int n, char* buf, int maxlen)
{
    proc_t* p = myproc();
    uint64 addr;
    arg_uint64(n, &addr);
    uvm_copyin_str(p->pgtbl, (uint64)buf, addr, maxlen);
}
