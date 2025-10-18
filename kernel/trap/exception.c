#include "trap/exception.h"
#include "trap/syscall.h"
#include "riscv.h"
#include "lib/print.h"
#include "mem/str.h"
#include "proc/scheduler.h"
#include "dev/timer_sched.h"
#include "proc/proc.h"

// 全局异常统计（必须定义）
struct exception_stats global_exception_stats;

// 异常名称映射
static const char* exception_names[] = {
    [CAUSE_INST_MISALIGNED]   = "Instruction misaligned",
    [CAUSE_INST_ACCESS_FAULT] = "Instruction access fault", 
    [CAUSE_ILLEGAL_INST]      = "Illegal instruction",
    [CAUSE_BREAKPOINT]        = "Breakpoint",
    [CAUSE_LOAD_MISALIGNED]   = "Load misaligned",
    [CAUSE_LOAD_ACCESS_FAULT] = "Load access fault",
    [CAUSE_STORE_MISALIGNED]  = "Store misaligned", 
    [CAUSE_STORE_ACCESS_FAULT]= "Store access fault",
    [CAUSE_ECALL_U]           = "Environment call from U-mode",
    [CAUSE_ECALL_S]           = "Environment call from S-mode",
    [CAUSE_INST_PAGE_FAULT]   = "Instruction page fault",
    [CAUSE_LOAD_PAGE_FAULT]   = "Load page fault",
    [CAUSE_STORE_PAGE_FAULT]  = "Store page fault",
};

// 异常处理初始化
void exception_init(void) {
    printf("=== Exception Handler Initialization ===\n");
    
    // 清零异常统计
    memset(&global_exception_stats, 0, sizeof(global_exception_stats));
    
    // 初始化系统调用
    syscall_init();
    
    printf("Exception handler initialized successfully\n");
}

// 获取异常名称
const char* exception_name(uint64 cause) {
    if (cause < sizeof(exception_names)/sizeof(exception_names[0]) && 
        exception_names[cause]) {
        return exception_names[cause];
    }
    return "Unknown exception";
}

// 打印异常上下文信息
void print_trapframe_new(struct trapframe *tf) {
    printf("=== Exception Context ===\n");
    printf("CPU: %d\n", mycpuid());
    printf("SCAUSE: 0x%lx (%s)\n", tf->scause, exception_name(tf->scause));
    printf("SEPC: 0x%lx\n", tf->sepc);
    printf("STVAL: 0x%lx\n", tf->stval);
    printf("SSTATUS: 0x%lx\n", tf->sstatus);
    printf("========================\n");
}

// === 新的主异常处理分发器 ===
void handle_exception_new(struct trapframe *tf) {
    uint64 cause = tf->scause;
    
    // 更新全局统计
    global_exception_stats.total_exceptions++;
    
    printf("CPU %d: Exception occurred - %s (cause: %lu)\n", 
           mycpuid(), exception_name(cause), cause);
    
    // 异常分发处理
    switch (cause) {
        case CAUSE_INST_MISALIGNED:
            global_exception_stats.inst_misaligned++;
            handle_inst_misaligned_new(tf);
            break;
            
        case CAUSE_INST_ACCESS_FAULT:
            global_exception_stats.inst_access_fault++;
            handle_inst_access_fault_new(tf);
            break;
            
        case CAUSE_ILLEGAL_INST:
            global_exception_stats.illegal_inst++;
            handle_illegal_inst_new(tf);
            break;
            
        case CAUSE_BREAKPOINT:
            global_exception_stats.breakpoint++;
            handle_breakpoint_new(tf);
            break;
            
        case CAUSE_LOAD_MISALIGNED:
            global_exception_stats.load_misaligned++;
            handle_load_misaligned_new(tf);
            break;
            
        case CAUSE_LOAD_ACCESS_FAULT:
            global_exception_stats.load_access_fault++;
            handle_load_access_fault_new(tf);
            break;
            
        case CAUSE_STORE_MISALIGNED:
            global_exception_stats.store_misaligned++;
            handle_store_misaligned_new(tf);
            break;
            
        case CAUSE_STORE_ACCESS_FAULT:
            global_exception_stats.store_access_fault++;
            handle_store_access_fault_new(tf);
            break;
            
        case CAUSE_ECALL_U:  // 系统调用
            global_exception_stats.ecall_u++;
            handle_syscall_new(tf);
            break;
            
        case CAUSE_ECALL_S:
            global_exception_stats.ecall_s++;
            handle_ecall_s_new(tf);
            break;
            
        case CAUSE_INST_PAGE_FAULT:
            global_exception_stats.inst_page_fault++;
            handle_instruction_page_fault_new(tf);
            break;
            
        case CAUSE_LOAD_PAGE_FAULT:
            global_exception_stats.load_page_fault++;
            handle_load_page_fault_new(tf);
            break;
            
        case CAUSE_STORE_PAGE_FAULT:
            global_exception_stats.store_page_fault++;
            handle_store_page_fault_new(tf);
            break;
            
        default:
            global_exception_stats.unknown_exceptions++;
            printf("CPU %d: Unknown exception cause: %lu\n", mycpuid(), cause);
            print_trapframe_new(tf);
            panic("Unknown exception");
    }
    
    printf("CPU %d: Exception handled successfully\n", mycpuid());
}

// === 具体异常处理函数实现 ===

void handle_inst_misaligned_new(struct trapframe *tf) {
    printf("CPU %d: Handling instruction misalignment at 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Instruction misaligned - program error");
}

void handle_inst_access_fault_new(struct trapframe *tf) {
    printf("CPU %d: Handling instruction access fault at 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Instruction access fault");
}

void handle_illegal_inst_new(struct trapframe *tf) {
    printf("CPU %d: Handling illegal instruction at PC=0x%lx\n", 
           mycpuid(), tf->sepc);
    print_trapframe_new(tf);
    
    // 尝试跳过这条指令（假设是4字节指令）
    tf->sepc += 4;
    printf("CPU %d: Skipping illegal instruction, continuing at 0x%lx\n", 
           mycpuid(), tf->sepc);
}

void handle_breakpoint_new(struct trapframe *tf) {
    printf("CPU %d: Breakpoint hit at PC=0x%lx\n", 
           mycpuid(), tf->sepc);
    print_trapframe_new(tf);
    
    // 跳过EBREAK指令
    tf->sepc += 4;
    printf("CPU %d: Continuing after breakpoint\n", mycpuid());
}

void handle_load_misaligned_new(struct trapframe *tf) {
    printf("CPU %d: Handling load misalignment at address 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Load misaligned - unsupported");
}

void handle_load_access_fault_new(struct trapframe *tf) {
    printf("CPU %d: Handling load access fault at address 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Load access fault");
}

void handle_store_misaligned_new(struct trapframe *tf) {
    printf("CPU %d: Handling store misalignment at address 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Store misaligned - unsupported");
}

void handle_store_access_fault_new(struct trapframe *tf) {
    printf("CPU %d: Handling store access fault at address 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Store access fault");
}

void handle_ecall_s_new(struct trapframe *tf) {
    printf("CPU %d: Handling S-mode environment call\n", mycpuid());
    print_trapframe_new(tf);
    panic("S-mode ECALL not implemented");
}

void handle_instruction_page_fault_new(struct trapframe *tf) {
    printf("CPU %d: Handling instruction page fault at 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Instruction page fault - virtual memory not fully implemented");
}

void handle_load_page_fault_new(struct trapframe *tf) {
    printf("CPU %d: Handling load page fault at 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Load page fault - virtual memory not fully implemented");
}

void handle_store_page_fault_new(struct trapframe *tf) {
    printf("CPU %d: Handling store page fault at 0x%lx\n", 
           mycpuid(), tf->stval);
    print_trapframe_new(tf);
    panic("Store page fault - virtual memory not fully implemented");
}

// 打印异常统计信息
void print_exception_stats(void) {
    printf("\n=== Exception Statistics ===\n");
    printf("Total exceptions: %lu\n", global_exception_stats.total_exceptions);
    printf("Instruction misaligned: %lu\n", global_exception_stats.inst_misaligned);
    printf("Instruction access fault: %lu\n", global_exception_stats.inst_access_fault);
    printf("Illegal instructions: %lu\n", global_exception_stats.illegal_inst);
    printf("Breakpoints: %lu\n", global_exception_stats.breakpoint);
    printf("Load misaligned: %lu\n", global_exception_stats.load_misaligned);
    printf("Load access fault: %lu\n", global_exception_stats.load_access_fault);
    printf("Store misaligned: %lu\n", global_exception_stats.store_misaligned);
    printf("Store access fault: %lu\n", global_exception_stats.store_access_fault);
    printf("System calls (U-mode): %lu\n", global_exception_stats.ecall_u);
    printf("Environment calls (S-mode): %lu\n", global_exception_stats.ecall_s);
    printf("Instruction page faults: %lu\n", global_exception_stats.inst_page_fault);
    printf("Load page faults: %lu\n", global_exception_stats.load_page_fault);
    printf("Store page faults: %lu\n", global_exception_stats.store_page_fault);
    printf("Unknown exceptions: %lu\n", global_exception_stats.unknown_exceptions);
    printf("============================\n");
}