# Lab-4：首个用户态进程创建

## U模式切换到S模式

- **过程硬件自动完成：**
  - 清除SIE位禁用中断。
  - 把pc拷贝到sepc。
  - 设置sstatus的SPP为当前特权（U模式为0，S模式为1）。
  - 设置scause为trap原因。
  - 设置特权模式为Supervisor。
  - 设置stvec为中断入口地址。
  - 退到中断处理程序。
- **软件负责部分：**
  - 软件需手动设置新的页表地址（satp）和保存上下文 (sepc以外的寄存器)。
  - 软件需准备内核页表和trapframe，完成寄存器保存和恢复。

## S模式切换到U模式

- **软件必须显式完成：**
  - 清除sstatus的SPP字段 = 0（切换到U模式）。
  - 将SPIE置1（开启用户态的中断）。
  - 将用户态入口PC写入sepc。
  - 设置satp寄存器，装入用户页表。
  - 在线程上下文恢复完成后执行`sret`，硬件自动完成从sstatus读取新权限并切换到U模式。

## 任务 1：深入理解进程抽象

### 分析 xv6 的进程结构体

**struct proc 各字段**

```
struct proc {
  struct spinlock lock;        // 进程锁保护
  
  // 需要锁保护的字段
  enum procstate state;        // 进程状态
  void *chan;                  // 等待通道（sleep/wakeup）
  int killed;                  // 杀死标志
  int xstate;                  // 退出状态
  int pid;                     // 进程ID
  
  // 需要wait_lock保护的字段
  struct proc *parent;         // 父进程指针
  
  // 进程私有字段（无需锁保护）
  uint64 kstack;               // 内核栈虚拟地址
  uint64 sz;                   // 进程内存大小
  pagetable_t pagetable;       // 用户页表
  struct trapframe *trapframe; // 陷阱帧
  struct context context;      // 调度上下文
  struct file *ofile[NOFILE];  // 打开的文件
  struct inode *cwd;           // 当前目录
  char name[16];               // 进程名称
};
```

| 字段      | 作用                                | 保护机制  |
| --------- | ----------------------------------- | --------- |
| lock      | 保护进程结构体的并发访问            | 自旋锁    |
| state     | 进程当前状态                        | p->lock   |
| chan      | sleep()时等待的通道，wakeup()时唤醒 | p->lock   |
| killed    | 标记进程是否被杀死                  | p->lock   |
| xstate    | 进程退出状态码                      | p->lock   |
| pid       | 唯一进程标识符                      | p->lock   |
| parent    | 父进程指针，用于wait()机制          | wait_lock |
| kstack    | 内核栈地址，每个进程独有            | 无需锁    |
| sz        | 用户空间大小                        | 无需锁    |
| pagetable | 用户页表，实现虚拟内存              | 无需锁    |
| trapframe | 保存用户寄存器状态                  | 无需锁    |
| context   | 内核线程上下文，用于调度切换        | 无需锁    |

**进程状态转换图**

```
        allocproc()
UNUSED ────────────> USED ──────────> RUNNABLE
                      │                  ^ │
                      │ userinit()       │ │ scheduler()
                      │ fork()           │ │
                      v                  │ v
              ┌─> ZOMBIE            RUNNING ──┐
              │      ^                  │     │
              │      │ exit()           │     │ yield()
   wait() ────┘      │                  │     │ sleep()
                     └──────────────────┘     │
                                              v
                                         SLEEPING
                                              │
                                              │ wakeup()
                                              v
                                         RUNNABLE
```

| 状态转换            | 触发条件       | 执行函数               |
| ------------------- | -------------- | ---------------------- |
| UNUSED → USED       | 分配新进程     | `allocproc()`          |
| USED → RUNNABLE     | 进程初始化完成 | `userinit()`, `fork()` |
| RUNNABLE → RUNNING  | 调度器选中     | `scheduler()`          |
| RUNNING → RUNNABLE  | 时间片用完     | `yield()`              |
| RUNNING → SLEEPING  | 等待资源       | `sleep()`              |
| SLEEPING → RUNNABLE | 资源可用       | `wakeup()`             |
| RUNNING → ZOMBIE    | 进程退出       | `exit()`               |
| ZOMBIE → UNUSED     | 父进程回收     | `wait()`               |

**锁保护**

锁的层次结构

```
// 锁的获取顺序（避免死锁）
1. wait_lock     // 保护父子关系
2. p->lock       // 保护进程状态
3. pid_lock      // 保护PID分配
```

并发问题示例

```
// 不安全的代码
void unsafe_kill(int pid) {
    struct proc *p = find_proc(pid);  // 可能被其他CPU释放
    p->killed = 1;                    // 竞争条件！
}

// 安全的代码
int kkill(int pid) {
    struct proc *p;
    for(p = proc; p < &proc[NPROC]; p++){
        acquire(&p->lock);            // 获取锁
        if(p->pid == pid){
            p->killed = 1;            // 原子操作
            if(p->state == SLEEPING){
                p->state = RUNNABLE;  // 唤醒进程
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}
```

**锁保护的必要性：**

1. **多CPU并发**: 多个CPU可能同时访问同一进程结构体
2. **原子操作**: 状态检查和修改必须是原子的
3. **一致性**: 防止读取到不一致的进程状态
4. **调度安全**: 防止进程在调度过程中被意外修改

### 理解进程生命周期

**UNUSED → USED**

```
static struct proc* allocproc(void) {
    for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if(p->state == UNUSED) {
            p->pid = allocpid();
            p->state = USED;        // 状态转换
            // 分配trapframe和页表
            return p;
        }
        release(&p->lock);
    }
}
```

**触发条件**: 系统需要创建新进程时

**USED → RUNNABLE**

```
void userinit(void) {
    struct proc *p = allocproc();
    // 初始化用户程序
    p->state = RUNNABLE;           // 状态转换
    release(&p->lock);
}
```

**触发条件**: 进程初始化完成，可以被调度

**RUNNABLE → RUNNING**

```
void scheduler(void) {
    for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if(p->state == RUNNABLE) {
            p->state = RUNNING;     // 状态转换
            c->proc = p;
            swtch(&c->context, &p->context);
        }
        release(&p->lock);
    }
}
```

**触发条件**: 调度器选中该进程执行

**RUNNING → RUNNABLE**

```
void yield(void) {
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;           // 状态转换
    sched();
    release(&p->lock);
}
```

**触发条件**: 主动让出CPU或时间片耗尽

**RUNNING → SLEEPING**

```
void sleep(void *chan, struct spinlock *lk) {
    struct proc *p = myproc();
    acquire(&p->lock);
    p->chan = chan;
    p->state = SLEEPING;           // 状态转换
    sched();
    release(&p->lock);
}
```

**触发条件**: 等待某个资源或条件

**SLEEPING → RUNNABLE**

```
void wakeup(void *chan) {
    for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if(p->state == SLEEPING && p->chan == chan) {
            p->state = RUNNABLE;    // 状态转换
        }
        release(&p->lock);
    }
}
```

**触发条件**: 等待的条件满足

**RUNNING → ZOMBIE**

```
void kexit(int status) {
    struct proc *p = myproc();
    p->xstate = status;
    p->state = ZOMBIE;             // 状态转换
    sched();
}
```

**触发条件**: 进程调用exit()退出

**需要原子保护**

**1. 状态转换操作**

```
acquire(&p->lock);
p->state = RUNNING;               // 必须原子
release(&p->lock);
```

**2. PID分配**

```
int allocpid() {
    acquire(&pid_lock);
    pid = nextpid++;              // 必须原子
    release(&pid_lock);
    return pid;
}
```

**3. 父子关系操作**

```
acquire(&wait_lock);
np->parent = p;                   // 必须原子
release(&wait_lock);
```

**4. 进程查找和修改**

```
// kill操作
acquire(&p->lock);
if(p->pid == target_pid) {
    p->killed = 1;                // 必须原子
}
release(&p->lock);
```

### 深入思考

**为什么需要ZOMBIE状态**

**1. 状态信息保存**

```
void kexit(int status) {
    struct proc *p = myproc();
    p->xstate = status;           // 保存退出状态
    p->state = ZOMBIE;            // 进入僵尸状态
    sched();                      // 不再返回
}

int kwait(uint64 addr) {
    for(pp = proc; pp < &proc[NPROC]; pp++){
        if(pp->parent == p && pp->state == ZOMBIE){
            pid = pp->pid;
            // 获取退出状态
            copyout(p->pagetable, addr, (char *)&pp->xstate, sizeof(pp->xstate));
            freeproc(pp);         // 完全释放
            return pid;
        }
    }
}
```

**ZOMBIE状态的必要性：**

- **状态传递**: 子进程需要将退出状态传递给父进程
- **防止资源泄漏**: 确保父进程能够回收子进程资源
- **同步机制**: 父子进程之间的同步点
- **进程表管理**: 避免进程表项过早释放

**2. 避免竞争条件**
如果没有ZOMBIE状态，进程退出后立即释放，可能导致：

- 父进程wait()时找不到子进程
- 退出状态丢失
- PID可能被立即重用，造成混乱

**进程表大小限制的影响**

```
#define NPROC 64  // 最大进程数

struct proc proc[NPROC];  // 静态数组
```

**1. 系统容量限制**

```
static struct proc* allocproc(void) {
    for(p = proc; p < &proc[NPROC]; p++) {  // O(n)搜索
        acquire(&p->lock);
        if(p->state == UNUSED) {
            return p;
        }
        release(&p->lock);
    }
    return 0;                     // 进程表满，分配失败
}
```

**2. 性能影响**

- **调度开销**: scheduler()需要遍历整个进程表
- **搜索效率**: 查找操作时间复杂度O(n)
- **锁竞争**: 频繁的锁获取和释放

**3. 内存占用**

```
// 每个进程结构体约占200-300字节
// 64个进程 ≈ 20KB内存
// 加上每个进程的内核栈(4KB) ≈ 256KB
```

**4. 可扩展性问题**

- 硬编码限制，无法动态调整
- 不适合大型系统或服务器环境
- 可能成为系统瓶颈

**如何防止PID重复**

```
int nextpid = 1;           // 全局PID计数器
struct spinlock pid_lock;  // PID分配锁

int allocpid() {
    int pid;
    
    acquire(&pid_lock);    // 原子操作
    pid = nextpid;
    nextpid = nextpid + 1; // 单调递增
    release(&pid_lock);
    
    return pid;
}
```

**防重复策略分析：**

**1. 单调递增策略**

- **优点**: 简单、高效、在系统运行期间绝不重复
- **缺点**: 可能溢出（理论上）

**2. 溢出处理**

```
// 改进版本
int allocpid() {
    int pid;
    acquire(&pid_lock);
    do {
        pid = nextpid++;
        if (nextpid <= 0) {       // 溢出处理
            nextpid = 1;
        }
    } while (pid_in_use(pid));    // 检查是否在使用
    release(&pid_lock);
    return pid;
}
```

**3. 其他可能的策略**

- **位图法**: 使用位图标记已用PID
- **循环分配**: PID用完后从1重新开始
- **随机分配**: 增加安全性，防止PID预测

### 其他

**context结构体**

```
struct context {
  uint64 ra;    // 返回地址
  uint64 sp;    // 栈指针
  // callee-saved registers
  uint64 s0-s11; // 保存的寄存器
};
```

用于内核线程切换，保存调度点的执行上下文。

**trapframe结构体**

```
struct trapframe {
  uint64 kernel_satp;   // 内核页表
  uint64 kernel_sp;     // 内核栈顶
  uint64 kernel_trap;   // trap处理函数
  uint64 epc;           // 用户程序计数器
  uint64 kernel_hartid; // CPU ID
  // 所有用户寄存器 ra, sp, gp, tp, t0-t6, s0-s11, a0-a7
};
```

用于用户态/内核态切换，保存完整的用户态执行环境。

**进程创建流程**

```
kfork() → allocproc() → proc_pagetable() → RUNNABLE
```

**进程调度流程**

```
scheduler() → swtch() → RUNNING → yield() → swtch() → scheduler()
```

**进程退出流程**

```
kexit() → ZOMBIE → wait() → freeproc() → UNUSED
```

## 任务 2：分析 xv6 的进程创建机制

### 研读 allocproc() 函数

```
// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}
```

**如何在进程表中找到空闲槽位？**

```
static struct proc* allocproc(void) {
  struct proc *p;

  // 线性搜索进程表
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);           // 获取进程锁
    if(p->state == UNUSED) {     // 找到空闲槽位
      goto found;
    } else {
      release(&p->lock);         // 释放锁继续搜索
    }
  }
  return 0;                      // 没有空闲槽位

found:
  // 找到空闲槽位，继续初始化...
}
```

搜索策略分析：

- **线性搜索**：从proc[0]开始顺序搜索
- **即时锁定**：找到候选槽位立即加锁，避免竞争
- **原子检查**：在持有锁的情况下检查state状态
- **时间复杂度**：O(n)，其中n是NPROC

**进程ID是如何分配的？**

```
int nextpid = 1;                    // 全局PID计数器
struct spinlock pid_lock;           // PID分配锁

int allocpid() {
  int pid;
  
  acquire(&pid_lock);               // 获取PID锁
  pid = nextpid;                    // 获取当前PID
  nextpid = nextpid + 1;            // 递增计数器
  release(&pid_lock);               // 释放锁

  return pid;
}
```

**PID分配机制：**

- **单调递增**：PID永不重复（在系统运行期间）
- **原子操作**：使用锁保证分配的原子性
- **线程安全**：多CPU环境下安全分配
- **简单高效**：无需复杂的回收和重用机制

**用户栈是如何设置的？**

xv6中用户栈的设置在proc_pagetable()和后续的内存分配中完成：

```
// 在proc_pagetable()中设置页表结构
pagetable_t proc_pagetable(struct proc *p) {
  pagetable_t pagetable;

  pagetable = uvmcreate();          // 创建空页表
  
  // 映射trampoline页面（最高虚拟地址）
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // 映射trapframe页面（trampoline下方）
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}
```

**用户栈设置过程：**

1. **页表创建**：创建空的用户页表
2. **特殊页面映射**：映射trampoline和trapframe
3. **用户栈分配**：通过uvmalloc()在后续分配用户栈
4. **虚拟地址布局**：用户栈位于高地址空间

**陷阱帧的初始化过程**

```
static struct proc* allocproc(void) {
  // ... 找到空闲槽位后 ...
  
  p->pid = allocpid();
  p->state = USED;

  // 分配陷阱帧页面
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 创建用户页表（包含trapframe映射）
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 设置内核上下文
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;      // 返回地址设为forkret
  p->context.sp = p->kstack + PGSIZE;   // 内核栈顶

  return p;
}
```

**陷阱帧初始化步骤：**

1. **物理内存分配**：kalloc()分配一个物理页面
2. **虚拟地址映射**：在用户页表中映射到TRAPFRAME地址
3. **结构体关联**：p->trapframe指向物理页面
4. **内容初始化**：在fork()或exec()时设置具体内容

### 深入理解 fork() 实现

```
// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}
```

**为什么父子进程有不同的返回值？**

```
// 在fork()中设置子进程返回值
np->trapframe->a0 = 0;          // 子进程返回0

// 父进程返回子进程PID
return pid;                     // 父进程返回子进程PID
```

**返回值机制：**

1. **寄存器机制**：RISC-V中a0寄存器存放函数返回值
2. **子进程设置**：直接修改子进程trapframe中的a0为0
3. **父进程返回**：通过正常的函数返回机制返回PID
4. **区分机制**：使父子进程能够知道自己的身份

```
int main() {
    int pid = fork();
    if (pid == 0) {
        // 子进程代码
        printf("I am child\n");
    } else if (pid > 0) {
        // 父进程代码
        printf("I am parent, child pid = %d\n", pid);
    } else {
        // fork失败
        printf("fork failed\n");
    }
}
```

**内存复制是如何实现的？**

```
// uvmcopy函数实现（在vm.c中）
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)      // 获取页表项
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)               // 检查页面有效性
      panic("uvmcopy: page not present");
    
    pa = PTE2PA(*pte);                    // 获取物理地址
    flags = PTE_FLAGS(*pte);              // 获取页面标志
    
    if((mem = kalloc()) == 0)             // 分配新物理页面
      goto err;
    
    memmove(mem, (char*)pa, PGSIZE);      // 复制页面内容
    
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);                         // 映射失败则释放内存
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);        // 清理已分配的页面
  return -1;
}
```

**内存复制过程：**

1. **页面遍历**：按页遍历父进程的用户内存
2. **物理页分配**：为每个页面分配新的物理内存
3. **内容复制**：使用memmove复制页面内容
4. **页表映射**：在子进程页表中建立映射
5. **错误处理**：失败时清理已分配的资源

**失败时的资源清理策略**

```
// allocproc中的清理
if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);                // 清理进程结构
    release(&p->lock);
    return 0;
}

// fork中的清理
if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);               // 清理新进程
    release(&np->lock);
    return -1;
}

// freeproc函数实现
static void freeproc(struct proc *p) {
  if(p->trapframe)
    kfree((void*)p->trapframe);         // 释放trapframe
  p->trapframe = 0;
  
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);  // 释放页表
  p->pagetable = 0;
  
  // 清零所有字段
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
}
```

**清理策略特点：**

- **即时清理**：一旦发现错误立即清理
- **完整清理**：释放所有已分配的资源
- **状态重置**：将进程状态重置为UNUSED
- **锁管理**：正确处理锁的获取和释放

### 分析进程退出机制

**exit() 与 wait() 的协作关系**

```
// exit()实现
void kexit(int status) {
  struct proc *p = myproc();

  // 1. 关闭所有打开的文件
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  // 2. 释放当前目录
  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // 3. 将子进程转移给init进程
  reparent(p);

  // 4. 唤醒父进程
  wakeup(p->parent);
  
  acquire(&p->lock);

  // 5. 设置退出状态
  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // 6. 调度其他进程，永不返回
  sched();
  panic("zombie exit");
}

// wait()实现
int kwait(uint64 addr) {
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    havekids = 0;
    // 扫描进程表寻找子进程
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        acquire(&pp->lock);
        havekids = 1;
        
        if(pp->state == ZOMBIE){
          // 找到僵尸子进程
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, 
                                  (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);         // 完全释放子进程
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // 没有子进程或被杀死
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // 等待子进程退出
    sleep(p, &wait_lock);
  }
}
```

**协作机制：**

1. **状态同步**：exit()设置ZOMBIE状态，wait()检测并回收
2. **通信机制**：通过wakeup()和sleep()实现同步
3. **数据传递**：通过xstate字段传递退出状态
4. **资源回收**：wait()负责最终的资源释放

**资源回收的时机和方式**

**exit()阶段回收：**

```
// 立即回收的资源
- 文件描述符：fileclose()
- 当前目录：iput()
- 用户内存：（在freeproc中回收）
```

**wait()阶段回收：**

```
// 延迟回收的资源
- 进程结构体：freeproc()
- 页表：proc_freepagetable()
- trapframe：kfree()
- 进程表槽位：状态设为UNUSED
```

**两阶段回收的原因：**

- **状态保持**：父进程需要获取子进程的退出状态
- **同步需要**：避免进程表项过早释放
- **资源管理**：确保所有资源都被正确释放

**孤儿进程的处理**

```
void reparent(struct proc *p) {
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;    // 转移给init进程
      wakeup(initproc);         // 唤醒init进程
    }
  }
}
```

**孤儿进程处理机制：**

1. **重新分配父进程**：将所有子进程的父进程设为init
2. **通知init进程**：通过wakeup()通知init进程
3. **init进程职责**：init进程负责回收所有孤儿进程
4. **防止资源泄漏**：确保所有进程最终都被回收

### 关键问题

**fork() 的性能瓶颈在哪里？**

**主要瓶颈：**

1. **内存复制开销**

```
// uvmcopy中的逐页复制
memmove(mem, (char*)pa, PGSIZE);    // 每页4KB的内存复制
```

1. **页表操作开销**

```
// 每个页面都需要页表操作
mappages(new, i, PGSIZE, (uint64)mem, flags);
```

1. **内存分配开销**

```
if((mem = kalloc()) == 0)           // 频繁的内存分配
```

**性能分析：**

- **时间复杂度**：O(n)，其中n是父进程的页面数
- **空间开销**：完全复制父进程的内存空间
- **I/O开销**：大量的内存读写操作

**如何实现写时复制优化？**

**写时复制（Copy-on-Write, COW）设计：**

```
// COW版本的uvmcopy
int uvmcopy_cow(pagetable_t old, pagetable_t new, uint64 sz) {
  pte_t *pte;
  uint64 pa, i;
  uint flags;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    
    // 关键：不分配新页面，共享同一物理页面
    // 将写权限改为COW标记
    flags = (flags & ~PTE_W) | PTE_COW;
    
    // 修改父进程页表
    *pte = PA2PTE(pa) | flags;
    
    // 映射到子进程页表（共享同一物理页面）
    if(mappages(new, i, PGSIZE, pa, flags) != 0)
      goto err;
    
    // 增加页面引用计数
    inc_ref_count(pa);
  }
  return 0;
}

// COW页面错误处理
void cow_fault_handler(uint64 va) {
  struct proc *p = myproc();
  pte_t *pte;
  uint64 pa, new_pa;
  uint flags;
  
  pte = walk(p->pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_COW) == 0)
    return;  // 不是COW页面错误
  
  pa = PTE2PA(*pte);
  
  if(get_ref_count(pa) == 1) {
    // 只有一个引用，直接恢复写权限
    flags = (PTE_FLAGS(*pte) & ~PTE_COW) | PTE_W;
    *pte = PA2PTE(pa) | flags;
  } else {
    // 多个引用，需要复制页面
    new_pa = (uint64)kalloc();
    if(new_pa == 0)
      panic("cow_fault_handler: out of memory");
    
    memmove((void*)new_pa, (void*)pa, PGSIZE);
    
    flags = (PTE_FLAGS(*pte) & ~PTE_COW) | PTE_W;
    *pte = PA2PTE(new_pa) | flags;
    
    dec_ref_count(pa);
  }
  
  sfence_vma();  // 刷新TLB
}
```

**COW优化效果：**

1. **fork()加速**：从O(n)降低到O(1)
2. **内存节省**：只在实际写入时才复制页面
3. **缓存友好**：减少不必要的内存访问
4. **适合场景**：许多子进程只读父进程数据的情况

**COW实现要点：**

- **页面标记**：使用PTE_COW标记共享页面
- **引用计数**：跟踪每个物理页面的引用数
- **错误处理**：在页面错误时执行实际复制
- **TLB管理**：页表修改后需要刷新TLB

## 任务 3：设计你的进程管理系统

### 1. 进程结构体设计

```
// 进程状态枚举
typedef enum {
    PROC_UNUSED = 0,    // 未使用
    PROC_EMBRYO,        // 正在创建
    PROC_RUNNABLE,      // 可运行
    PROC_RUNNING,       // 正在运行
    PROC_SLEEPING,      // 睡眠等待
    PROC_ZOMBIE         // 僵尸状态
} proc_state_t;

// 基础进程结构体（现在实现）
typedef struct proc {
    // 基本标识信息
    int pid;                    // 进程ID
    proc_state_t state;         // 进程状态
    
    // 内存管理
    pgtbl_t pgtbl;             // 用户态页表
    uint64 heap_top;           // 用户堆顶（以字节为单位）
    uint64 ustack_pages;       // 用户栈占用的页面数量
    trapframe_t* tf;           // 用户态内核态切换时的运行环境暂存空间
    
    // 调度上下文
    uint64 kstack;             // 内核栈的虚拟地址
    context_t ctx;             // 内核态进程上下文
    
    // 同步与通信（预留扩展）
    void* wait_chan;           // 等待通道
    int exit_code;             // 退出状态码
    
    // 进程关系（基本支持）
    struct proc* parent;       // 父进程指针
    int killed;                // 被杀死标志
    
    // 扩展预留字段
    struct {
        uint64 reserved[4];    // 预留空间，便于后续扩展
    } ext;
    
} proc_t;
```

- 保持与PPT中定义的核心字段一致
- 添加必要的状态管理字段
- 预留扩展空间，避免破坏ABI兼容性
- 结构体大小控制在合理范围

### 2. 进程表组织方式

```
// 进程表配置
#define MAX_PROC 64           // 最大进程数

// 主进程表（静态数组）
static proc_t proc_table[MAX_PROC];

// 状态索引（便于快速查找）
static struct {
    proc_t* runnable_head;    // 可运行进程链表头
    proc_t* sleeping_head;    // 睡眠进程链表头
    int free_count;           // 空闲进程数量
    spinlock_t lock;          // 进程表锁
} proc_mgr;

// 扩展字段（添加到proc_t中）
typedef struct proc {
    // ... 原有字段 ...
    
    // 链表指针（用于状态索引）
    struct proc* next;        // 同状态进程链表
    struct proc* prev;        // 双向链表支持
    
} proc_t;
```

- 数组提供固定大小限制和连续内存
- 状态链表优化特定操作（如调度）
- 便于调试和进程表遍历
- 为后续哈希表扩展留出空间

### 3. 进程ID分配策略

```
// PID管理
static struct {
    int next_pid;             // 下一个PID
    spinlock_t lock;          // PID分配锁
} pid_mgr = {
    .next_pid = 1,            // 从1开始分配
};

// PID分配函数
int alloc_pid(void) {
    acquire(&pid_mgr.lock);
    
    int start_pid = pid_mgr.next_pid;
    do {
        int pid = pid_mgr.next_pid;
        pid_mgr.next_pid++;
        
        // 处理溢出（简单版本）
        if (pid_mgr.next_pid <= 0) {
            pid_mgr.next_pid = 1;
        }
        
        // 检查PID是否被使用
        if (!is_pid_in_use(pid)) {
            release(&pid_mgr.lock);
            return pid;
        }
        
    } while (pid_mgr.next_pid != start_pid);
    
    release(&pid_mgr.lock);
    return -1;  // 无可用PID
}

// PID使用检查（O(n)但简单可靠）
static int is_pid_in_use(int pid) {
    for (int i = 0; i < MAX_PROC; i++) {
        if (proc_table[i].state != PROC_UNUSED && 
            proc_table[i].pid == pid) {
            return 1;
        }
    }
    return 0;
}
```

- 避免PID重复使用带来的问题
- 处理整数溢出情况
- 为后续位图优化预留接口

### 4. 核心接口设计

```
// 进程管理基本接口
struct proc* alloc_process(void);           // 分配进程结构
void free_process(struct proc *p);          // 释放进程资源
int create_process(void (*entry)(void));    // 创建新进程
void exit_process(int status);              // 终止当前进程
int wait_process(int *status);              // 等待子进程

// 扩展接口（便于后续功能添加）
struct proc* find_process(int pid);         // 查找进程
void process_sleep(void* chan);             // 进程睡眠
void process_wakeup(void* chan);            // 唤醒进程
void process_yield(void);                   // 主动让出CPU
```

### 问题1：进程表用数组还是链表？

**选择：静态数组 + 状态链表**

理由：

- 数组提供固定大小限制，防止资源耗尽
- 数组便于调试和进程表遍历
- 状态链表优化调度器性能
- 为后续扩展（如哈希表）保留灵活性

### 问题2：如何高效查找特定PID的进程？

**初期方案：线性搜索**

```
struct proc* find_process(int pid) {
    acquire(&proc_mgr.lock);
    for (int i = 0; i < MAX_PROC; i++) {
        if (proc_table[i].state != PROC_UNUSED && 
            proc_table[i].pid == pid) {
            release(&proc_mgr.lock);
            return &proc_table[i];
        }
    }
    release(&proc_mgr.lock);
    return NULL;
}
```

**扩展方案：预留哈希表接口**

```
// 预留的哈希表结构
struct pid_hash_entry {
    proc_t* proc;
    struct pid_hash_entry* next;
};

// 在proc_mgr中预留
static struct {
    // ... 现有字段 ...
    struct pid_hash_entry* hash_table[16];  // 预留哈希表
} proc_mgr;
```

### 问题3：是否需要进程组和会话的概念？

**现阶段：不需要**

理由：

- 专注核心功能实现
- 降低初期复杂度
- 在进程结构体中预留扩展空间

**后续扩展：**

```
// 在ext.reserved中可以添加
struct proc_extension {
    int pgid;          // 进程组ID
    int sid;           // 会话ID
    uint32 flags;      // 进程标志
    // ... 其他扩展字段
};
```

### 问题4：如何处理进程资源限制？

**现阶段：简单硬编码限制**

```
// 资源限制常量
#define MAX_PROC_MEMORY    (16 * 1024 * 1024)  // 16MB
#define MAX_PROC_PAGES     (MAX_PROC_MEMORY / PGSIZE)
#define MAX_USER_STACK     (8 * PGSIZE)        // 32KB栈

// 资源检查函数
int check_memory_limit(proc_t* p, uint64 new_size) {
    return new_size <= MAX_PROC_MEMORY;
}

int check_stack_limit(proc_t* p, uint64 stack_pages) {
    return stack_pages <= (MAX_USER_STACK / PGSIZE);
}
```

**扩展方案：**

```
// 在进程结构体扩展区域添加
struct resource_limit {
    uint64 max_memory;
    uint64 max_stack;
    uint64 max_files;
    uint64 cpu_time_limit;
};
```

### 任务1：首个进程proczero的定义和初始化

- [x] 完善进程结构体定义
- [x] 实现 `proc_pgtbl_init()` 函数
- [x] 实现 `proc_make_first()` 函数
- [x] 配置trampoline和kstack映射

### 任务2：用户态陷阱处理

- [x] 实现 `trap_user_handler()`
- [x] 实现 `trap_user_return()`
- [x] 完成trampoline.S中的用户态切换
- [x] 处理第一个系统调用

### 问题

`KSTACK(0)`：

```
KSTACK(0) = TRAMPOLINE_VA - 2*PGSIZE - 0*2*PGSIZE
          = 0x3FFFFFF000 - 2*0x1000 - 0
          = 0x3FFFFFF000 - 0x2000
          = 0x3FFFFFD000
```

所以：

- `kstack_va = 0x3FFFFFD000`
- `kernel_sp = kstack_va + PGSIZE = 0x3FFFFFD000 + 0x1000 = 0x3FFFFFE000`

**问题**：`kernel_sp = 0x3FFFFFE000` 正好等于 `TRAPFRAME = 0x3FFFFFE000`！

内核栈顶和 trapframe 地址冲突

### 第一步：设置 Trap 向量

**目标：** 配置用户态异常处理入口

```
// kernel/trap/trap.c
void trap_user_init()
{
    printf("Setting up user trap vector...\n");
    
    extern char trampoline[];
    extern char user_vector[];
    
    uint64 trampoline_base = (uint64)trampoline;
    uint64 user_vector_addr = (uint64)user_vector;
    uint64 vector_offset = user_vector_addr - trampoline_base;
    
    uint64 stvec_addr = TRAMPOLINE + vector_offset;
    w_stvec(stvec_addr);
    
    printf("User trap vector set to: 0x%lx\n", stvec_addr);
}
```

### 第二步：实现 Trampoline 汇编代码

**目标：** 处理用户态-内核态切换的底层汇编

```
# kernel/trap/trampoline.S
.section trampsec

.globl trampoline
trampoline:
        nop
        nop
        nop
        nop

.align 4
.globl user_vector
user_vector:
        # 保存用户寄存器到 trapframe
        csrrw a0, sscratch, a0
        
        # 保存 sepc
        csrr t0, sepc
        sd t0, 24(a0)
        
        # 保存所有通用寄存器
        sd ra, 40(a0)
        sd sp, 48(a0)
        sd gp, 56(a0)
        # ... 其他寄存器
        sd a7, 168(a0)          # 系统调用号
        
        # 保存原始 a0
        csrr t0, sscratch
        sd t0, 112(a0)
        
        # 切换到内核栈和页表
        ld sp, 8(a0)            # kernel_sp
        ld tp, 32(a0)           # kernel_hartid
        ld t1, 0(a0)            # kernel_satp
        csrw satp, t1
        sfence.vma zero, zero
        
        # 跳转到 trap handler
        ld t0, 16(a0)           # kernel_trap
        jr t0

.globl user_return
user_return:
        # 恢复用户页表
        csrw satp, a1
        sfence.vma zero, zero
        
        # 恢复 sepc
        ld t0, 24(a0)
        csrw sepc, t0
        
        # 恢复所有寄存器
        ld ra, 40(a0)
        ld sp, 48(a0)
        # ... 其他寄存器
        
        csrrw a0, sscratch, a0
        sret
```

### 第三步：实现 Trap Handler

**目标：** 处理系统调用逻辑

```
// kernel/trap/trap_user.c
void trap_user_handler()
{
    uint64 sepc = r_sepc();
    uint64 scause = r_scause();
    
    proc_t* p = myproc();
    
    if (scause == 8) {  // ecall from U-mode
        uint64 syscall_num = p->tf->a7;
        
        switch (syscall_num) {
            case 0:
                printf("✅ Syscall 0 - Test syscall\n");
                p->tf->a0 = 42;
                break;
            case 1:
                printf("✅ Syscall 1 - Another test\n");
                p->tf->a0 = 12345;
                break;
            case 3:
                printf("✅ Syscall 3 - Entering infinite loop\n");
                p->tf->epc = sepc;  // 重复执行
                p->tf->a0 = 0;
                trap_user_return();
                return;
        }
        
        // 更新 epc 跳过 ecall
        p->tf->epc = sepc + 4;
        trap_user_return();
    }
}

void trap_user_return()
{
    proc_t* p = myproc();
    
    // 设置返回用户态的状态
    uint64 sstatus = r_sstatus();
    sstatus &= ~(1UL << 8);   // 清除 SPP
    sstatus |= (1UL << 5);    // 设置 SPIE
    w_sstatus(sstatus);
    
    // 设置寄存器
    w_sepc(p->tf->epc);
    w_sscratch((uint64)p->tf);
    
    // 直接返回用户态（不切换页表）
    asm volatile(
        "ld sp, 48(%0)\n"
        "sret\n"
        :
        : "r" ((uint64)p->tf)
        : "memory"
    );
}
```

### 第四步：创建用户页表

**目标：** 为用户进程建立独立的地址空间

```
// kernel/proc/proc.c
pgtbl_t proc_pgtbl_init(uint64 trapframe_pa)
{
    pgtbl_t pgtbl = create_pagetable();
    
    // 映射 trampoline 页面
    extern char trampoline[];
    if (map_page(pgtbl, TRAMPOLINE, (uint64)trampoline, PTE_R | PTE_X) != 0) {
        panic("failed to map trampoline");
    }
    
    // 映射 trapframe 页面
    if (map_page(pgtbl, TRAPFRAME, trapframe_pa, PTE_R | PTE_W) != 0) {
        panic("failed to map trapframe");
    }
    
    return pgtbl;
}
```

### 第五步：创建用户进程

**目标：** 初始化第一个用户进程

```
// kernel/proc/proc.c
void proc_make_first()
{
    proc_t* p = &proczero;
    
    // 1. 基本设置
    p->pid = 1;
    p->state = PROC_EMBRYO;
    strcpy(p->name, "init");
    
    // 2. 分配 trapframe
    p->tf = (trapframe_t*)pmem_alloc(false);
    memset(p->tf, 0, sizeof(trapframe_t));
    
    // 3. 分配内核栈
    uint64 kstack_pa = (uint64)pmem_alloc(true);
    p->kstack = kstack_pa;
    
    // 4. 创建用户页表
    p->pgtbl = proc_pgtbl_init((uint64)p->tf);
    
    // 5. 映射内核栈到内核页表
    extern pagetable_t kernel_pagetable;
    uint64 kstack_va = KSTACK(0);
    map_page(kernel_pagetable, kstack_va, kstack_pa, PTE_R | PTE_W);
    
    // 6. 创建用户程序
    uint64 code_va = USER_TEXT_BASE;  // 0x1000
    uint64 code_pa = (uint64)pmem_alloc(false);
    
    unsigned int* code_ptr = (unsigned int*)code_pa;
    code_ptr[0] = 0x00000893;  // li a7, 0
    code_ptr[1] = 0x00000073;  // ecall
    code_ptr[2] = 0x00100893;  // li a7, 1
    code_ptr[3] = 0x00000073;  // ecall
    code_ptr[4] = 0x00300893;  // li a7, 3
    code_ptr[5] = 0x00000073;  // ecall
    code_ptr[6] = 0xfe9ff06f;  // j -24 (跳回开头)
    
    // 7. 映射用户代码
    map_page(p->pgtbl, code_va, code_pa, PTE_R | PTE_X | PTE_U);
    // 关键：在内核页表中也映射，添加 PTE_U 标志
    map_page(kernel_pagetable, code_va, code_pa, PTE_R | PTE_X | PTE_U);
    
    // 8. 映射用户栈
    uint64 ustack_va = USER_STACK_TOP - PGSIZE;
    uint64 ustack_pa = (uint64)pmem_alloc(false);
    map_page(p->pgtbl, ustack_va, ustack_pa, PTE_R | PTE_W | PTE_U);
    
    // 9. 设置 trapframe
    p->tf->kernel_satp = MAKE_SATP(kernel_pagetable);
    p->tf->kernel_sp = kstack_va + PGSIZE;
    p->tf->kernel_trap = (uint64)trap_user_handler;
    p->tf->kernel_hartid = mycpuid();
    p->tf->epc = code_va;           // 用户程序入口
    p->tf->sp = USER_STACK_TOP;     // 用户栈顶
    
    // 10. 启动进程
    cpu_t* cpu = mycpu();
    cpu->proc = p;
    p->state = PROC_RUNNING;
    
    w_sscratch((uint64)p->tf);  // 设置 trapframe 地址
    trap_user_return();         // 切换到用户态
}
```

### 最终效果

![image-20251108162417857](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251108162417857.png)

![image-20251108162428991](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251108162428991.png)

![image-20251108162442900](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251108162442900.png)

![image-20251108162456143](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251108162456143.png)

![image-20251108162509274](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251108162509274.png)

![image-20251108162525557](C:\Users\BOSS0\AppData\Roaming\Typora\typora-user-images\image-20251108162525557.png)
