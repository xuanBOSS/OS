# Lab-6：进程调度

## 1. 核心数据结构分析

### struct context - 上下文切换结构

```
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};
```

**作用**：保存进程切换时需要保存的寄存器状态。只保存callee-saved寄存器，因为caller-saved寄存器由调用者负责保存。

### struct cpu - CPU状态结构

```
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};
```

**作用**：维护每个CPU核心的状态，支持多核调度。

### struct trapframe - 陷阱帧结构

```
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};
```

这个结构保存用户态到内核态切换时的所有寄存器状态，包括：

- 内核相关信息（satp, sp, trap, hartid）
- 用户程序计数器（epc）
- 所有通用寄存器

### struct proc - 进程控制块

```
struct proc {
  struct spinlock lock;        // 进程锁
  enum procstate state;        // 进程状态
  void *chan;                 // 睡眠通道
  int killed;                 // 终止标志
  int xstate;                 // 退出状态
  int pid;                    // 进程ID
  struct proc *parent;        // 父进程
  uint64 kstack;              // 内核栈
  uint64 sz;                  // 内存大小
  pagetable_t pagetable;      // 页表
  struct trapframe *trapframe; // 陷阱帧
  struct context context;     // 上下文
  struct file *ofile[NOFILE]; // 打开文件
  struct inode *cwd;          // 当前目录
  char name[16];              // 进程名
};
```

------

## 2. 进程状态转换

```
      ①allocproc()        ②scheduler()         ③sleep()
unused ---------> runnable ---------> running ---------> sleeping
  ↑                   ↑                  ↓                  ↓
  │⑦freeproc()        │②wakeup()         │⑤exit()           │④wakeup()
  │                   │                  ↓                  ↓
  └─────────────── zombie <──────────── running <──────── runnable
                      ↑⑥wait()
```

- **UNUSED**: 进程槽未使用
- **USED**: 进程正在创建中
- **RUNNABLE**: 就绪状态，等待调度
- **RUNNING**: 正在运行
- **SLEEPING**: 等待某个事件
- **ZOMBIE**: 已退出，等待父进程回收

**①UNUSED → RUNNABLE (allocproc)**

- 进程创建时，`allocproc()`分配新的进程槽
- 跳过USED状态，直接设置为RUNNABLE

**②RUNNABLE → RUNNING (scheduler)**

- 调度器选中就绪进程
- 执行上下文切换

**③RUNNING → SLEEPING (sleep)**

- 进程主动调用`sleep()`等待某个事件
- 如等待I/O、等待锁等

**④SLEEPING → RUNNABLE (wakeup)**

- 等待的事件发生
- `wakeup()`唤醒睡眠进程

**⑤RUNNING → ZOMBIE (exit)**

- 进程调用`exit()`终止
- 保留进程信息等待父进程回收

**⑥ZOMBIE → UNUSED (wait)**

- 父进程调用`wait()`回收子进程
- 释放进程资源

**⑦特殊情况**

- 某些错误情况下可能直接从ZOMBIE回到UNUSED

------

## 3. 上下文切换机制（swtch.S）

```
.globl swtch
swtch:
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        ret
```

**关键点**：

- 只保存/恢复callee-saved寄存器
- 通过修改sp切换栈
- 通过修改ra改变返回地址

------

## 4. 系统调用接口（sysproc.c）

- `sys_fork()`: 调用`kfork()`创建子进程
- `sys_exit()`: 调用`kexit()`终止进程
- `sys_wait()`: 调用`kwait()`等待子进程
- `sys_kill()`: 调用`kkill()`发送信号

------

## 5. 操作系统进程调度流程

### 5.1 调度器的基本流程

```
1. 时钟中断触发
2. 保存当前进程状态
3. 调用调度器选择下一个进程
4. 上下文切换到新进程
5. 新进程开始/继续执行
```

### 5.2 详细调度过程

#### A. 进程创建（fork）

```
// 简化的fork流程
int fork() {
    struct proc *np = allocproc();  // 分配新进程
    // 复制父进程内存空间
    // 复制文件描述符
    // 设置父子关系
    np->state = RUNNABLE;           // 设为就绪状态
    return np->pid;
}
```

#### B. 进程调度（scheduler）

```
// 简化的调度器循环
void scheduler() {
    for(;;) {
        // 遍历进程表
        for(p = proc; p < &proc[NPROC]; p++) {
            if(p->state == RUNNABLE) {
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->context);  // 上下文切换
                c->proc = 0;
            }
        }
    }
}
```

#### C. 进程退出（exit）

```
// 简化的exit流程
void exit(int status) {
    // 关闭所有文件
    // 释放内存
    // 设置退出状态
    p->xstate = status;
    p->state = ZOMBIE;
    wakeup(p->parent);  // 唤醒父进程
    sched();            // 调度其他进程
}
```

### 5.3 调度策略

xv6使用**简单轮转调度**：

- 遍历进程表寻找RUNNABLE进程
- 按顺序调度，没有优先级
- 时间片用完或主动让出CPU时切换

### 5.4 同步机制

#### 睡眠/唤醒机制

```
// 进程睡眠
void sleep(void *chan, struct spinlock *lk) {
    p->chan = chan;
    p->state = SLEEPING;
    sched();  // 切换到其他进程
}

// 唤醒等待某个通道的所有进程
void wakeup(void *chan) {
    for(p = proc; p < &proc[NPROC]; p++) {
        if(p->state == SLEEPING && p->chan == chan) {
            p->state = RUNNABLE;
        }
    }
}
```

### 5.5 多核调度

每个CPU核心运行独立的调度器：

- 每个CPU有自己的`struct cpu`
- 进程可以在任意CPU上运行
- 使用锁保护共享的进程表

------

## RISC-V三种特权模式详解

### 1. 特权级别层次

```
M-mode (Machine)     - 特权级别最高，可以访问所有硬件
    ↑
S-mode (Supervisor)  - 操作系统内核运行的模式
    ↑  
U-mode (User)        - 用户程序运行的模式，特权级别最低
```

### 2. 各模式的职责和权限

#### **M-mode (机器模式)**

- **最高特权级**，可以访问所有硬件资源
- 职责：
  - 系统启动和初始化
  - 处理机器级异常和中断
  - 管理物理内存保护(PMP)
  - 处理不能被S-mode处理的异常
- **典型使用**：bootloader、固件、hypervisor

#### **S-mode (监管者模式)**

- **操作系统内核**运行的模式
- 职责：
  - 进程管理和调度
  - 虚拟内存管理
  - 系统调用处理
  - 设备驱动管理
- **限制**：不能直接访问某些机器级寄存器

#### **U-mode (用户模式)**

- **最低特权级**，用户程序运行模式
- 限制：
  - 不能执行特权指令
  - 不能直接访问硬件
  - 只能通过系统调用请求内核服务

### 3. 特权模式切换机制

#### **切换触发条件**

**向上切换（提升特权级）**：

1. **系统调用** (U→S): `ecall`指令
2. **异常** (U→S 或 S→M): 页错误、非法指令等
3. **中断** (任意→S/M): 时钟中断、外部中断等

**向下切换（降低特权级）**：

1. **异常返回** (S→U): `sret`指令
2. **机器返回** (M→S): `mret`指令

#### **切换的硬件机制**

**关键CSR寄存器**：

```
// S-mode相关寄存器
sstatus    // 状态寄存器，包含SPP(Previous Privilege)位
sepc       // 异常程序计数器，保存异常发生时的PC
scause     // 异常原因
stval      // 异常值
sscratch   // 临时寄存器
stvec      // 异常向量表基址

// M-mode相关寄存器  
mstatus    // 机器状态寄存器
mepc       // 机器异常程序计数器
mcause     // 机器异常原因
mtval      // 机器异常值
mscratch   // 机器临时寄存器
mtvec      // 机器异常向量表基址
```

### 4. 具体切换流程

#### **U-mode → S-mode (系统调用)**

```
# 用户程序执行
ecall                    # 触发系统调用

# 硬件自动执行：
# 1. 保存当前特权级到sstatus.SPP
# 2. 设置当前特权级为S-mode
# 3. 保存PC到sepc
# 4. 跳转到stvec指向的异常处理程序
# 5. 禁用中断(sstatus.SIE = 0)
```

#### **S-mode → U-mode (系统调用返回)**

```
# 内核处理完系统调用后
sret                     # 返回用户模式

# 硬件自动执行：
# 1. 从sstatus.SPP恢复之前的特权级
# 2. 从sepc恢复PC
# 3. 恢复中断状态
# 4. 跳转到用户程序继续执行
```

#### **异常处理流程**

```
// 1. 硬件保存上下文
sstatus.SPP = 当前特权级
sepc = 当前PC
scause = 异常原因
stval = 异常相关值

// 2. 跳转到异常处理程序
PC = stvec

// 3. 软件处理异常
void trap_handler() {
    // 保存寄存器到trapframe
    // 根据scause分发处理
    // 恢复寄存器
    // sret返回
}
```

### 5. 在操作系统中的应用

#### **典型的执行流程**：

```
1. 系统启动 (M-mode)
   ↓
2. 启动内核 (S-mode)  
   ↓
3. 创建用户进程 (U-mode)
   ↓
4. 用户程序执行系统调用 (U→S)
   ↓
5. 内核处理后返回 (S→U)
   ↓
6. 时钟中断触发调度 (U→S)
   ↓
7. 进程切换后返回 (S→U)
```

#### **关键设计要点**：

1. **异常向量表**：`stvec`指向统一的异常入口
2. **上下文保存**：软件负责保存/恢复通用寄存器
3. **特权级检查**：硬件自动检查指令执行权限
4. **内存保护**：通过页表和特权级控制内存访问

------

## 任务 1：上下文切换机制实现

在 RISC-V 架构中，寄存器分为两类：

- **调用者保存寄存器**：`ra, t0-t6, a0-a7`，由调用函数负责保存
- **被调用者保存寄存器**：`sp, s0-s11`，由被调用函数负责保存

**为什么不保存所有寄存器？**

1. **效率考虑**：调用者保存寄存器在函数调用时已经被保存到栈上
2. **职责分离**：遵循 RISC-V ABI 规范，避免重复保存
3. **性能优化**：减少上下文切换的开销

```
typedef struct {
    uint64 ra;  // 返回地址 - 进程恢复执行的位置
    uint64 sp;  // 栈指针 - 内核栈的位置
    // 被调用者保存寄存器 - 保证函数调用语义正确
    uint64 s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
} context_t;
```

**设计理由**：

- `ra`：保存进程的返回地址，通常指向 `forkret` 或调度点
- `sp`：保存内核栈指针，确保栈的连续性
- `s0-s11`：保存被调用者保存寄存器，维护函数调用约定

**上下文切换函数**

```
# proc/swtch.S
/*
    上下文切换
    void swtch(context_t *old, context_t *new);
*/

.globl swtch
swtch:
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        ret
```

**内核栈 vs 用户栈**：

- **内核栈**：每个进程独立的内核栈，用于系统调用和中断处理
- **用户栈**：进程的用户态栈，映射在用户虚拟地址空间

```
// 进程结构中的栈管理
typedef struct proc {
    uint64 kstack;          // 内核栈基地址
    pagetable_t pgtbl;      // 页表，包含用户栈映射
    trapframe_t *tf;        // 陷阱帧，保存用户态寄存器
    context_t ctx;          // 内核态上下文
} proc_t;
```

**栈切换的关键点**：

1. **原子性保证**：上下文切换在关中断状态下进行
2. **栈指针管理**：`sp` 寄存器的保存和恢复确保栈的连续性
3. **内存安全**：每个进程的内核栈独立分配，避免栈溢出影响其他进程

------

## 任务 2：调度器实现

采用简单轮转调度（Round Robin）策略：

- **公平性**：每个进程获得相等的 CPU 时间
- **简单性**：实现简单，易于理解和调试
- **响应性**：避免进程长时间等待

**调度器核心逻辑**

```
void scheduler(void) {
    proc_t *p;
    cpu_t *c = mycpu();
    
    c->proc = 0;
    
    for(;;) {
        // 开启中断，允许设备中断和时钟中断
        intr_on();
        
        // 遍历进程表，寻找可运行进程
        for (p = proc_table; p < &proc_table[MAX_PROC]; p++) {
            spinlock_acquire(&p->lock);
            
            if (p->state == PROC_RUNNABLE) {
                // 状态转换：RUNNABLE → RUNNING
                p->state = PROC_RUNNING;
                c->proc = p;
                
                // 为首次运行的进程初始化上下文
                if (p->ctx.sp == 0) {
                    p->ctx.sp = p->kstack;
                    p->ctx.ra = (uint64)forkret;
                }
                
                // 执行上下文切换
                swtch(&c->ctx, &p->ctx);
                
                // 进程返回后清理
                c->proc = 0;
            }
            
            spinlock_release(&p->lock);
        }
    }
}
```

#### 中断管理

**为什么需要开启中断？**

1. **设备响应**：允许设备中断处理 I/O 请求
2. **时钟中断**：支持抢占式调度（虽然当前实现主要是协作式）
3. **系统响应性**：避免系统完全无响应

#### 锁机制

```
// 进程锁的使用模式
spinlock_acquire(&p->lock);  // 获取进程锁
// 检查和修改进程状态
if (p->state == PROC_RUNNABLE) {
    p->state = PROC_RUNNING;
    // 执行上下文切换
}
spinlock_release(&p->lock);  // 释放进程锁
```

**锁的作用**：

- 保护进程状态的原子性修改
- 防止多 CPU 同时调度同一进程
- 确保进程状态的一致性

#### 主动调度

```
// yield() - 主动让出 CPU
uint64 sys_yield(void) {
    yield();
    return 0;
}

void yield(void) {
    proc_t *p = myproc();
    spinlock_acquire(&p->lock);
    p->state = PROC_RUNNABLE;
    sched();  // 切换到调度器
    spinlock_release(&p->lock);
}
```

#### 被动调度

进程在以下情况下被动调度：

- **系统调用阻塞**：如 `sys_wait` 中的 `sleep`
- **进程退出**：`sys_exit` 后进程变为僵尸状态
- **资源等待**：等待 I/O 或其他资源

------

## 任务 3：进程同步原语实现

Sleep/Wakeup 是一种条件变量机制：

- **Sleep**：进程等待某个条件满足时主动阻塞
- **Wakeup**：条件满足时唤醒等待的进程
- **Channel**：使用内存地址作为等待通道标识

```
// sleep() - 进程阻塞等待
void sleep(void *chan, spinlock_t *lk) {
    proc_t *p = myproc();
    
    // 必须持有进程锁
    spinlock_acquire(&p->lock);
    
    // 释放传入的锁（避免死锁）
    spinlock_release(lk);
    
    // 设置等待通道和状态
    p->wait_chan = chan;
    p->state = PROC_SLEEPING;
    
    // 让出 CPU
    sched();
    
    // 被唤醒后清理
    p->wait_chan = 0;
    spinlock_release(&p->lock);
    
    // 重新获取原来的锁
    spinlock_acquire(lk);
}

// wakeup() - 唤醒等待进程
void wakeup(void *chan) {
    for (proc_t *p = proc_table; p < &proc_table[MAX_PROC]; p++) {
        if (p != myproc() && p->pid > 0) {
            spinlock_acquire(&p->lock);
            
            if (p->state == PROC_SLEEPING && p->wait_chan == chan) {
                p->state = PROC_RUNNABLE;  // 唤醒进程
                p->wait_chan = 0;
            }
            
            spinlock_release(&p->lock);
        }
    }
}
```

**问题描述**：如果在检查条件和调用 `sleep` 之间发生了 `wakeup`，进程可能永远不会被唤醒。

**解决方案**：

1. **锁保护**：在检查条件和调用 `sleep` 之间持有锁
2. **原子操作**：`sleep` 函数原子地释放锁并阻塞进程
3. **锁传递**：将锁传递给 `sleep` 函数处理

```
// sys_wait 中的使用示例
uint64 sys_wait(void) {
    proc_t *p = myproc();
    
    spinlock_acquire(&wait_lock);
    
    for (;;) {
        // 检查是否有僵尸子进程
        int havekids = 0;
        for (child = proc_table; child < &proc_table[MAX_PROC]; child++) {
            if (child->parent == p) {
                havekids = 1;
                if (child->state == PROC_ZOMBIE) {
                    // 找到僵尸子进程，清理并返回
                    int pid = child->pid;
                    // 清理子进程...
                    spinlock_release(&wait_lock);
                    return pid;
                }
            }
        }
        
        if (!havekids) {
            spinlock_release(&wait_lock);
            return -1;  // 没有子进程
        }
        
        // 等待子进程退出
        sleep(p, &wait_lock);  // 在 wait_lock 保护下睡眠
    }
}
```

### 管道

#### 1. 数据结构定义

**`include/fs/file.h`** 中定义管道结构：

```
struct pipe {
    spinlock_t lock;
    char data[512];     // 环形缓冲区
    uint32 nread;       // 已读取的字节数
    uint32 nwrite;      // 已写入的字节数
    int readopen;       // 读端是否打开
    int writeopen;      // 写端是否打开
};
```

#### 2. 核心函数实现

**`kernel/fs/pipe.c`**：

| 函数          | 功能                             |
| ------------- | -------------------------------- |
| `pipealloc()` | 分配管道和两个文件结构           |
| `pipeclose()` | 关闭管道端，两端都关闭时释放内存 |
| `pipewrite()` | 写入数据，满时睡眠等待           |
| `piperead()`  | 读取数据，空时睡眠等待           |

#### 3. 系统调用集成

- **`sys_pipe()`**：创建管道，返回两个文件描述符
- 修改 **`fileread()`** / **`filewrite()`** / **`fileclose()`** 支持管道类型

#### 4. 用户接口

- 系统调用号：`SYS_pipe = 22`
- 用户函数：`int pipe(int fd[2])`

#### 测试

```
#include "common.h"
#include "sys.h"

void _start(void) {
    int pipefd[2];
    
    // 创建管道
    if (pipe(pipefd) < 0) {
        exit(-1);
    }
    
    int pid = fork();
    if (pid == 0) {
        // 子进程：写入数据
        close(pipefd[0]);
        int data = 42;
        write(pipefd[1], &data, sizeof(data));
        close(pipefd[1]);
        exit(0);
    } else if (pid > 0) {
        // 父进程：读取数据
        close(pipefd[1]);
        int data = 0;
        read(pipefd[0], &data, sizeof(data));
        close(pipefd[0]);
        
        int status;
        wait(&status);
        
        exit(data);  // 应该返回 42
    } else {
        exit(-1);
    }
}
```

![image-20251121162740602](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121162740602.png)

![image-20251121162754555](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121162754555.png)

![image-20251121162812177](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121162812177.png)

![image-20251121162827090](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121162827090.png)

![image-20251121162859211](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121162859211.png)

![image-20251121162914222](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121162914222.png)

![image-20251121162926571](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121162926571.png)

1. 管道创建成功 

```
sys_pipe: creating pipe
pipealloc: pipe allocated successfully
sys_pipe: allocated fd0=3 (read), fd1=4 (write)
```

2. 进程 fork 成功 

```
sys_fork: parent PID=2 creating child
sys_fork: child PID=3 created successfully
```

3. 子进程写入数据 

```
pipewrite: writing 4 bytes
pipewrite: wrote 4 bytes
```

4. 父进程阻塞等待数据 

```
piperead: reading up to 4 bytes
piperead: no data, sleeping
```

5. 子进程退出，唤醒父进程 

```
sys_exit: process new_proc (PID=3) exiting with status 0
pipeclose: write end closed
```

6. 父进程读取成功 

```
sleep: [WOKE UP] - sched() returned!
piperead: read 4 bytes
```

7. 父进程以 data (42) 退出 

```
sys_exit: process init (PID=2) exiting with status 42
```

管道的**生产者-消费者同步**工作：

| 时间线 | 父进程 (PID=2)      | 子进程 (PID=3)    |
| ------ | ------------------- | ----------------- |
| T1     | 创建管道 fd=3,4     | -                 |
| T2     | fork 子进程         | 被创建            |
| T3     | close(4), read(3)   | close(3)          |
| T4     | **睡眠等待数据**    | write(4, 42)      |
| T5     | -                   | close(4), exit(0) |
| T6     | **被唤醒，读取 42** | -                 |
| T7     | wait(), exit(42)    | 被回收            |

------

1. **进程在 `sleep()` 中获取锁**
2. **调用 `sched()` 时锁仍被持有**
3. **切换到调度器，锁仍被持有**
4. **调度器找到可运行进程，切换回去**
5. **进程从 `sched()` 返回，锁仍被持有**
6. **`sleep()` 正确释放锁**

**yield 系统调用**

**yield** 是一个**主动让出 CPU** 的系统调用，它的作用是：

1. **协作式调度**：进程主动放弃当前的 CPU 时间片
2. **提高响应性**：让其他进程有机会运行
3. **避免独占 CPU**：防止计算密集型任务长时间占用 CPU
4. **测试调度器**：验证进程调度机制是否正常工作

**yield 的工作原理**

```
void yield(void) {
    proc_t *p = myproc();           // 获取当前进程
    spinlock_acquire(&p->lock);     // 获取进程锁
    p->state = PROC_RUNNABLE;       // 状态：RUNNING → RUNNABLE
    sched();                        // 切换到调度器
    spinlock_release(&p->lock);     // 释放进程锁（恢复时执行）
}
```

**执行流程**：

1. 当前进程调用 `yield()`
2. 进程状态从 `RUNNING` 变为 `RUNNABLE`
3. 调用 `sched()` 切换到调度器
4. 调度器选择其他可运行的进程
5. 当前进程重新被调度时，从 `sched()` 返回
6. 释放锁，继续执行

------

## 测试

### 测试1：基础 fork/wait 测试

```
// user/simple_test.c - 基础测试版本
#include "sys.h"

void _start(void) {
    int pid = fork();
    if (pid == 0) {
        exit(42);
    } else {
        int status;
        wait(&status);
        exit(0);
    }
}
```

![image-20251121145202464](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145202464.png)

![image-20251121145218601](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145218601.png)

![image-20251121145236401](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145236401.png)

![image-20251121145306097](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145306097.png)

![image-20251121145318940](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145318940.png)

### 测试2：调度器测试

```
// user/simple_test.c - 调度器测试版本
#include "sys.h"

void cpu_intensive_task(int task_id) {
    volatile int sum = 0;
    
    for (int i = 0; i < 50000; i++) {
        sum += i * i;
        
        if (i % 10000 == 0) {
            yield();  // 主动让出CPU
        }
    }
    
    exit(task_id + 10);  // 退出码: 10, 11, 12
}

void _start(void) {
    // 创建3个计算密集型进程
    for (int i = 0; i < 3; i++) {
        int pid = fork();
        
        if (pid == 0) {
            cpu_intensive_task(i);
        } else if (pid < 0) {
            exit(-1);
        }
    }
    
    // 等待所有子进程
    for (int i = 0; i < 3; i++) {
        int status;
        wait(&status);
    }
    
    exit(0);
}
```

| 功能                   | 状态 | 说明                           |
| ---------------------- | ---- | ------------------------------ |
| **多进程创建**         | 通过 | 3 个子进程全部创建成功         |
| **并发执行**           | 通过 | 进程在 CPU 上交替执行          |
| **调度公平性**         | 通过 | 轮转调度，每个进程都有机会运行 |
| **`yield()` 系统调用** | 通过 | 主动让出 CPU 正常工作          |
| **计算密集型任务**     | 通过 | 50000 次循环正常完成           |
| **进程退出**           | 通过 | 退出码正确传递                 |
| **父进程等待**         | 通过 | `wait()` 正确获取子进程状态    |
| **资源回收**           | 通过 | 所有进程资源正确释放           |

![image-20251121153123728](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153123728.png)

![image-20251121153136193](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153136193.png)

![image-20251121153150328](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153150328.png)

![image-20251121153206352](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153206352.png)

![image-20251121153222308](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153222308.png)

![image-20251121153233702](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153233702.png)

![image-20251121153249289](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153249289.png)

![image-20251121153303951](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153303951.png)

![image-20251121153317784](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153317784.png)

![image-20251121153329045](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153329045.png)

![image-20251121153340583](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153340583.png)

![image-20251121153358179](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153358179.png)

![image-20251121153412294](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153412294.png)

![image-20251121153423541](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153423541.png)

![image-20251121153434472](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153434472.png)

![image-20251121153452741](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153452741.png)

![image-20251121153500471](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121153500471.png)

### 测试3：同步机制测试

```
#include "common.h"
#include "sys.h"

#define NUM_ITEMS 20

void _start(void);  // 前置声明

// ========================================
// 主函数（必须在最前面）
// ========================================
void _start(void) {
    int pipefd[2];
    
    // 创建管道
    if (pipe(pipefd) < 0) {
        exit(-1);
    }
    
    int read_fd = pipefd[0];
    int write_fd = pipefd[1];
    
    // 创建生产者进程
    int pid1 = fork();
    if (pid1 == 0) {
        // 子进程1：生产者
        close(read_fd);
        
        // 写入 0-19 到管道
        for (int i = 0; i < NUM_ITEMS; i++) {
            write(write_fd, &i, sizeof(i));
            
            // 模拟生产时间
            for (volatile int j = 0; j < 10000; j++);
            
            // 偶尔让出 CPU
            if (i % 5 == 0) {
                yield();
            }
        }
        
        close(write_fd);
        exit(0);  // 生产者退出码 0
        
    } else if (pid1 < 0) {
        exit(-2);
    }
    
    // 创建消费者进程
    int pid2 = fork();
    if (pid2 == 0) {
        // 子进程2：消费者
        close(write_fd);
        
        int data;
        int count = 0;
        int sum = 0;
        
        // 从管道读取数据
        while (read(read_fd, &data, sizeof(data)) == sizeof(data)) {
            sum += data;
            count++;
            
            // 模拟消费时间
            for (volatile int j = 0; j < 10000; j++);
            
            // 偶尔让出 CPU
            if (count % 5 == 0) {
                yield();
            }
        }
        
        close(read_fd);
        
        // 验证：count 应该是 20，sum 应该是 190 (0+1+...+19)
        // 返回 count 作为退出码
        exit(count);
        
    } else if (pid2 < 0) {
        exit(-3);
    }
    
    // 父进程：关闭两端并等待子进程
    close(read_fd);
    close(write_fd);
    
    // 等待两个子进程
    int status1, status2;
    wait(&status1);  // 等待第一个结束的子进程
    wait(&status2);  // 等待第二个结束的子进程
    
    // 其中一个是生产者（返回 0），另一个是消费者（返回 20）
    int consumer_count = (status1 > status2) ? status1 : status2;
    
    // 返回消费者读取的数量（期望 20）
    exit(consumer_count);
}
```

```
1. 管道创建 ✅
sys_pipe: allocated fd0=3 (read), fd1=4 (write)

2. 进程创建 ✅
sys_fork: child PID=3 created successfully  # 生产者
sys_fork: child PID=4 created successfully  # 消费者

3. 生产者写入 20 次 ✅
pipewrite: wrote 4 bytes  (×20)

4. 消费者读取 20 次 ✅
piperead: read 4 bytes  (×20)

5. 同步机制验证 ✅
消费者阻塞等待：
piperead: no data, sleeping
sleep: [START] PID=4 on chan=0x803dd218
生产者写入后，消费者被唤醒：
sleep: [WOKE UP] - sched() returned!
piperead: read 4 bytes

6. EOF 检测 ✅
生产者关闭写端：
pipeclose: write end closed
消费者检测到 EOF：
piperead: read 0 bytes
fileread: returning 0 bytes

7. 退出状态正确 ✅
sys_exit: process (PID=3) exiting with status 0   # 生产者
sys_exit: process (PID=4) exiting with status 20  # 消费者（count=20）
sys_exit: process (PID=2) exiting with status 20  # 父进程返回消费者结果
```

1. **生产者-消费者模型**

- 生产者：写入 20 个整数（0-19）
- 消费者：读取并计数

2. **同步机制**

- 管道空时，消费者自动睡眠
- 管道满时（512 字节），生产者会睡眠（当前数据量小，不会触发）
- 写端关闭后，消费者读到 EOF（返回 0）

3. **并发执行**

- 使用 `yield()` 模拟抢占式调度
- 生产和消费交替进行

4. **资源清理**

- 管道两端都关闭后自动释放
- 子进程退出后被父进程回收

### 测试4：进程树测试

```
// user/simple_test.c - 进程树测试版本
#include "sys.h"

void _start(void) {
    int child1 = fork();
    if (child1 == 0) {
        // 子进程创建孙进程
        int grandchild1 = fork();
        if (grandchild1 == 0) {
            exit(31);  // 孙进程1
        } else if (grandchild1 > 0) {
            int grandchild2 = fork();
            if (grandchild2 == 0) {
                exit(32);  // 孙进程2
            } else if (grandchild2 > 0) {
                int status1, status2;
                wait(&status1);
                wait(&status2);
                exit(21);  // 子进程1
            } else {
                exit(-31);
            }
        } else {
            exit(-21);
        }
    } else if (child1 > 0) {
        int child2 = fork();
        if (child2 == 0) {
            exit(22);  // 子进程2
        } else if (child2 > 0) {
            int status1, status2;
            wait(&status1);
            wait(&status2);
            exit(0);  // 父进程
        } else {
            exit(-22);
        }
    } else {
        exit(-1);
    }
}
```

```
           PID=2 (init)
          /            \
      PID=3          PID=4
      /    \
  PID=5  PID=6
```

1. **PID=2 创建两个子进程**：
   - `sys_fork: parent PID=2 creating child` → PID=3
   - `sys_fork: parent PID=2 creating child` → PID=4
2. **PID=3 创建两个孙进程**：
   - `sys_fork: parent PID=3 creating child` → PID=5
   - `sys_fork: parent PID=3 creating child` → PID=6
3. **孙进程先退出**：
   - `sys_exit: process new_proc (PID=5) exiting with status 31` 
   - `sys_exit: process new_proc (PID=6) exiting with status 32` 
4. **PID=3 等待并回收孙进程**：
   - `sys_wait: found zombie child PID=5, exit_status=31` 
   - `sys_wait: found zombie child PID=6, exit_status=32` 
5. **PID=3 退出**：
   - `sys_exit: process new_proc (PID=3) exiting with status 21` 
6. **PID=4 退出**：
   - `sys_exit: process new_proc (PID=4) exiting with status 22` 
7. **PID=2 回收所有子进程**：
   - `sys_wait: found zombie child PID=4, exit_status=22` 
   - `sys_wait: found zombie child PID=3, exit_status=21` 
8. **PID=2 退出**：
   - `sys_exit: process init (PID=2) exiting with status 0` 

![image-20251121145837091](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145837091.png)

![image-20251121145855184](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145855184.png)

![image-20251121145914745](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145914745.png)

![image-20251121145929192](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145929192.png)

![image-20251121145945015](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121145945015.png)

![image-20251121150012047](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150012047.png)

![image-20251121150025668](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150025668.png)

![image-20251121150038826](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150038826.png)

![image-20251121150056544](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150056544.png)

![image-20251121150109703](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150109703.png)

![image-20251121150123507](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150123507.png)

![image-20251121150137286](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150137286.png)

![image-20251121150154709](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150154709.png)

![image-20251121150212284](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150212284.png)

![image-20251121150227082](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251121150227082.png)

### 测试5：压力测试

```
// user/simple_test.c - 压力测试版本
#include "sys.h"

void _start(void) {
    const int num_children = 5;
    
    for (int i = 0; i < num_children; i++) {
        int pid = fork();
        
        if (pid == 0) {
            volatile int sum = 0;
            int work_amount = (i + 1) * 20000;
            
            for (int j = 0; j < work_amount; j++) {
                sum += j;
                if (j % 5000 == 0) {
                    yield();
                }
            }
            
            exit(i + 50);  // 退出码: 50-54
        } else if (pid < 0) {
            break;
        }
    }
    
    // 等待所有子进程
    int completed = 0;
    int status;
    while (wait(&status) > 0) {
        completed++;
    }
    
    exit(completed);
}
```

1. 创建了 5 个子进程

```
sys_fork: child PID=3 created successfully
sys_fork: child PID=4 created successfully
sys_fork: child PID=5 created successfully
sys_fork: child PID=6 created successfully
sys_fork: child PID=7 created successfully
```

2. 最终退出码是 5

```
exit_code=5
```

**说明父进程成功等待并回收了 5 个子进程**

------

## 思考题

### 1. 调度策略

#### 轮转调度的公平性如何？

**公平性分析：**

- **优点**：每个进程获得相等的时间片，简单公平
- 缺点：
  - 不区分 I/O 密集型和 CPU 密集型进程
  - 不考虑进程优先级
  - 交互式进程响应可能较慢

**改进方案**：

```
// 多级反馈队列
struct proc {
    int priority;      // 优先级 0-3
    int time_slice;    // 剩余时间片
    int cpu_burst;     // CPU 使用统计
};

// 动态调整优先级
void adjust_priority(proc_t *p) {
    if (p->cpu_burst > THRESHOLD) {
        p->priority--;  // CPU 密集型降低优先级
    } else {
        p->priority++;  // I/O 密集型提高优先级
    }
}
```

#### 如何实现实时调度？

**实时调度要求**：任务必须在截止时间内完成

**实现方案**：

```
// 实时进程结构
struct rt_proc {
    uint64 deadline;     // 截止时间
    uint64 period;       // 周期
    int rt_priority;     // 实时优先级 (0-99)
};

// EDF (Earliest Deadline First) 调度
proc_t* edf_schedule(void) {
    proc_t *earliest = NULL;
    uint64 min_deadline = UINT64_MAX;
    
    for (proc_t *p = procs; p < &procs[NPROC]; p++) {
        if (p->state == RUNNABLE && p->is_realtime) {
            if (p->deadline < min_deadline) {
                min_deadline = p->deadline;
                earliest = p;
            }
        }
    }
    return earliest;
}
```

### 2. 性能优化

#### fork() 的性能瓶颈如何解决？

**当前瓶颈**：复制整个地址空间

**解决方案：写时复制 (Copy-on-Write)**

```
// 1. fork 时只复制页表，共享物理页
int fork_cow(void) {
    proc_t *child = proc_alloc();
    
    // 复制页表，但物理页共享
    for (va = 0; va < parent->sz; va += PGSIZE) {
        pte_t *pte = walk(parent->pagetable, va);
        uint64 pa = PTE2PA(*pte);
        
        // 标记为只读
        *pte &= ~PTE_W;
        *pte |= PTE_COW;  // COW 标志
        
        // 子进程映射到同一物理页
        map_page(child->pagetable, va, pa, flags & ~PTE_W);
        
        // 增加引用计数
        page_ref[pa / PGSIZE]++;
    }
    return child->pid;
}

// 2. 写入时触发缺页异常，再复制
void cow_fault(uint64 va) {
    pte_t *pte = walk(myproc()->pagetable, va);
    uint64 old_pa = PTE2PA(*pte);
    
    if (page_ref[old_pa / PGSIZE] > 1) {
        // 复制页面
        uint64 new_pa = kalloc();
        memmove((void*)new_pa, (void*)old_pa, PGSIZE);
        page_ref[old_pa / PGSIZE]--;
        
        // 更新映射
        *pte = PA2PTE(new_pa) | PTE_W | (flags & ~PTE_COW);
    } else {
        // 只有一个引用，直接设为可写
        *pte |= PTE_W;
        *pte &= ~PTE_COW;
    }
}
```

#### 上下文切换开销如何降低？

**优化方法**：

```
// 1. 减少保存的寄存器数量
// 只保存 callee-saved 寄存器 (s0-s11, ra, sp)

// 2. 使用轻量级线程
// 同一进程内的线程共享地址空间，切换时不需要切换页表
void thread_switch(thread_t *from, thread_t *to) {
    // 不需要: sfence.vma, 不需要切换 satp
    swtch(&from->ctx, &to->ctx);  // 只切换寄存器
}

// 3. 批量处理系统调用
// 减少用户态/内核态切换次数
struct syscall_batch {
    int calls[16];
    uint64 args[16][6];
    int count;
};

// 4. 延迟 TLB 刷新
// 使用 ASID 避免每次切换都刷新 TLB
#define MAKE_SATP_ASID(pt, asid) (SATP_SV39 | ((asid) << 44) | ((uint64)(pt) >> 12))
```

### 3. 资源管理

#### 如何实现进程资源限制？

```
// 资源限制结构
struct rlimit {
    uint64 max_memory;      // 最大内存 (bytes)
    uint64 max_files;       // 最大文件描述符数
    uint64 max_cpu_time;    // 最大 CPU 时间 (ticks)
    uint64 max_children;    // 最大子进程数
};

struct proc {
    // ... 其他字段
    struct rlimit limits;
    uint64 used_memory;
    uint64 used_cpu_time;
    int num_children;
};

// 检查资源限制
int check_limit(proc_t *p, int resource, uint64 request) {
    switch (resource) {
        case RLIMIT_MEMORY:
            if (p->used_memory + request > p->limits.max_memory)
                return -1;  // 超出限制
            break;
        case RLIMIT_NPROC:
            if (p->num_children >= p->limits.max_children)
                return -1;
            break;
    }
    return 0;
}

// setrlimit 系统调用
int sys_setrlimit(int resource, struct rlimit *rlim) {
    proc_t *p = myproc();
    // 只能降低限制，不能提高（除非是 root）
    if (rlim->max_memory > p->limits.max_memory && p->uid != 0)
        return -1;
    p->limits = *rlim;
    return 0;
}
```

#### 如何处理进程资源泄漏？

```
// 1. 进程退出时强制清理
void exit(int status) {
    proc_t *p = myproc();
    
    // 关闭所有文件
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            fileclose(p->ofile[fd]);
            p->ofile[fd] = NULL;
        }
    }
    
    // 释放所有内存
    proc_free_memory(p);
    
    // 释放信号量、锁等
    release_all_locks(p);
    
    // 子进程交给 init
    reparent_children(p);
}

// 2. 定期检查孤儿进程
void reaper_thread(void) {
    while (1) {
        for (proc_t *p = procs; p < &procs[NPROC]; p++) {
            if (p->state == ZOMBIE && p->parent == NULL) {
                proc_free(p);  // 回收孤儿僵尸进程
            }
        }
        sleep_ms(1000);  // 每秒检查一次
    }
}

// 3. 资源引用计数
struct file {
    int ref;      // 引用计数
    // ...
};

void fileclose(struct file *f) {
    if (--f->ref == 0) {
        // 真正释放资源
        kfree(f);
    }
}
```

### 4. 扩展性

#### 如何支持多核调度？

```
// 每个 CPU 有独立的调度队列
struct cpu {
    int id;
    proc_t *proc;           // 当前运行的进程
    context_t ctx;          // 调度器上下文
    struct spinlock lock;   // CPU 锁
    
    // 本地运行队列
    proc_t *runqueue[NPROC];
    int runqueue_size;
};

struct cpu cpus[NCPU];

// 获取当前 CPU
struct cpu* mycpu(void) {
    int id = r_tp();  // 读取 hart ID
    return &cpus[id];
}

// 多核调度器
void scheduler(void) {
    struct cpu *c = mycpu();
    
    while (1) {
        intr_on();
        
        acquire(&c->lock);
        
        // 先从本地队列选择
        proc_t *p = NULL;
        if (c->runqueue_size > 0) {
            p = c->runqueue[--c->runqueue_size];
        }
        
        if (p == NULL) {
            // 本地队列空，尝试窃取
            p = steal_from_other_cpu();
        }
        
        if (p) {
            p->state = RUNNING;
            c->proc = p;
            swtch(&c->ctx, &p->ctx);
            c->proc = NULL;
        }
        
        release(&c->lock);
    }
}
```

#### 如何实现负载均衡？

```
// 1. 工作窃取 (Work Stealing)
proc_t* steal_from_other_cpu(void) {
    int my_id = mycpu()->id;
    
    for (int i = 0; i < NCPU; i++) {
        if (i == my_id) continue;
        
        struct cpu *other = &cpus[i];
        acquire(&other->lock);
        
        // 窃取一半的任务
        if (other->runqueue_size > 1) {
            int steal_count = other->runqueue_size / 2;
            proc_t *stolen = other->runqueue[--other->runqueue_size];
            release(&other->lock);
            return stolen;
        }
        
        release(&other->lock);
    }
    return NULL;
}

// 2. 负载统计和迁移
struct load_info {
    int runnable_count;     // 可运行进程数
    uint64 cpu_usage;       // CPU 使用率
    uint64 last_update;     // 上次更新时间
};

void balance_load(void) {
    // 找到负载最重和最轻的 CPU
    int max_cpu = 0, min_cpu = 0;
    int max_load = 0, min_load = INT_MAX;
    
    for (int i = 0; i < NCPU; i++) {
        int load = cpus[i].runqueue_size;
        if (load > max_load) { max_load = load; max_cpu = i; }
        if (load < min_load) { min_load = load; min_cpu = i; }
    }
    
    // 负载差异超过阈值时迁移
    if (max_load - min_load > 2) {
        migrate_process(&cpus[max_cpu], &cpus[min_cpu]);
    }
}

// 3. CPU 亲和性
struct proc {
    uint64 cpu_affinity;  // 位图，表示可以运行在哪些 CPU 上
};

int can_run_on(proc_t *p, int cpu_id) {
    return (p->cpu_affinity >> cpu_id) & 1;
}

// 设置 CPU 亲和性
int sys_sched_setaffinity(int pid, uint64 mask) {
    proc_t *p = find_proc(pid);
    if (p) {
        p->cpu_affinity = mask;
        return 0;
    }
    return -1;
}
```

### 总结

| 问题       | 核心解决思路                   |
| ---------- | ------------------------------ |
| 轮转公平性 | 多级反馈队列 + 动态优先级      |
| 实时调度   | EDF/RM 算法 + 优先级抢占       |
| fork 性能  | 写时复制 (COW)                 |
| 上下文切换 | ASID、批量系统调用、轻量级线程 |
| 资源限制   | rlimit 结构 + 检查点           |
| 资源泄漏   | 引用计数 + 强制清理            |
| 多核调度   | Per-CPU 运行队列               |
| 负载均衡   | 工作窃取 + 定期迁移            |