#ifndef __TRAPFRAME_H__
#define __TRAPFRAME_H__

#ifndef uint64
typedef unsigned long long uint64;
#endif

// Task 4：中断/异常上下文保存结构
struct trapframe {
    uint64 reg[32];     // x0-x31通用寄存器 (256字节)
    uint64 sepc;        // 异常程序计数器 (8字节)
    uint64 sstatus;     // 状态寄存器 (8字节)
    uint64 scause;      // 异常原因 (8字节)
    uint64 stval;       // 异常值 (8字节)
};

// trapframe访问宏
#define TF_REG(tf, n)    ((tf)->reg[n])
#define TF_RA(tf)        ((tf)->reg[1])
#define TF_SP(tf)        ((tf)->reg[2])
#define TF_GP(tf)        ((tf)->reg[3])
#define TF_TP(tf)        ((tf)->reg[4])
#define TF_A0(tf)        ((tf)->reg[10])
#define TF_A1(tf)        ((tf)->reg[11])

// 栈管理常量
#define STACK_GUARD_SIZE     4096    
#define MAX_STACK_DEPTH      8       
#define TRAPFRAME_SIZE       288
#define STACK_FRAME_SIZE     512

// Task 4 新增函数声明
void kerneltrap(struct trapframe *tf);
void handle_exception(struct trapframe *tf, int trap_id);
int get_current_interrupt_depth(void);
void print_interrupt_stack_info(void);
void dump_trapframe(struct trapframe *tf);

// 栈管理函数声明
void interrupt_stack_enter(void);
void interrupt_stack_exit(void);
int check_stack_overflow(void);

// 64位安全打印函数声明
void print_hex64(uint64 value);
void print_hex64_padded(uint64 value);
void print_dec64(uint64 value);
void print_size64(uint64 size_bytes);

#endif // __TRAPFRAME_H__