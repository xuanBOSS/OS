#include "syscall/syscall_table.h"
#include "syscall/sysfunc.h"
#include "syscall/sysnum.h"
#include "fs/stat.h"

// 测试系统调用声明
extern uint64 sys_test_basic(void);
extern uint64 sys_test_args(void);
extern uint64 sys_test_error(void);
extern uint64 sys_test_privilege(void);
extern uint64 sys_test_pointer(void);
extern uint64 sys_test_string(void);

extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_kill(void);
extern uint64 sys_getpid(void);
extern uint64 sys_open(void);
extern uint64 sys_close(void);
extern uint64 sys_read(void);
extern uint64 sys_write(void);
extern uint64 sys_sbrk(void);

extern uint64 sys_lseek(void);
extern uint64 sys_stat(void);
extern uint64 sys_fstat(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_chdir(void);
extern uint64 sys_getcwd(void);
extern uint64 sys_link(void);
extern uint64 sys_unlink(void);
extern uint64 sys_dup(void);
extern uint64 sys_dup2(void);

syscall_desc_t syscall_table[64] = {
    // === 测试系统调用 ===
    [10] = {
        .func = sys_test_basic,
        .name = "test_basic",
        .arg_count = 0,
        .need_proc = true,
        .min_privilege = 0
    },
    
    [11] = {
        .func = sys_test_args,
        .name = "test_args",
        .arg_count = 3,
        .args = {
            {ARG_UINT64, 0, false},
            {ARG_UINT64, 0, false},
            {ARG_UINT64, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [12] = {
        .func = sys_test_error,
        .name = "test_error",
        .arg_count = 1,
        .args = {
            {ARG_UINT64, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [13] = {
        .func = sys_test_privilege,
        .name = "test_privilege",
        .arg_count = 0,
        .need_proc = true,
        .min_privilege = 1  // 需要权限级别1
    },
    
    [14] = {
        .func = sys_test_pointer,
        .name = "test_pointer",
        .arg_count = 2,
        .args = {
            {ARG_PTR, 0, false},
            {ARG_UINT64, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [15] = {
        .func = sys_test_string,
        .name = "test_string",
        .arg_count = 1,
        .args = {
            {ARG_STRING, 64, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    // === 原有系统调用 ===
    SYSCALL_ENTRY(SYS_brk, sys_brk, "brk", 1,
        {ARG_UINT64, 0, true}
    ),
    
    SYSCALL_ENTRY(SYS_mmap, sys_mmap, "mmap", 2,
        {ARG_UINT64, 0, true},
        {ARG_UINT64, 0, false}
    ),
    
    SYSCALL_ENTRY(SYS_munmap, sys_munmap, "munmap", 2,
        {ARG_UINT64, 0, false},
        {ARG_UINT64, 0, false}
    ),
    
    SYSCALL_ENTRY(SYS_copyin, sys_copyin, "copyin", 2,
        {ARG_PTR, 0, false},
        {ARG_UINT64, 0, false}
    ),
    
    SYSCALL_ENTRY(SYS_copyout, sys_copyout, "copyout", 1,
        {ARG_PTR, 0, false}
    ),
    
    SYSCALL_ENTRY(SYS_copyinstr, sys_copyinstr, "copyinstr", 1,
        {ARG_STRING, 64, false}
    ),

    // 进程控制类
    [SYS_fork] = {
        .func = sys_fork,
        .name = "fork",
        .arg_count = 0,
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_exit] = {
        .func = sys_exit,
        .name = "exit",
        .arg_count = 1,
        .args = {
            {ARG_INT, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_wait] = {
        .func = sys_wait,
        .name = "wait",
        .arg_count = 1,
        .args = {
            {ARG_PTR, sizeof(int), true}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_kill] = {
        .func = sys_kill,
        .name = "kill",
        .arg_count = 2,
        .args = {
            {ARG_INT, 0, false},
            {ARG_INT, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_getpid] = {
        .func = sys_getpid,
        .name = "getpid",
        .arg_count = 0,
        .need_proc = true,
        .min_privilege = 0
    },
    
    // 文件操作类
    [SYS_open] = {
        .func = sys_open,
        .name = "open",
        .arg_count = 2,
        .args = {
            {ARG_STRING, 256, false},
            {ARG_INT, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_close] = {
        .func = sys_close,
        .name = "close",
        .arg_count = 1,
        .args = {
            {ARG_INT, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_read] = {
        .func = sys_read,
        .name = "read",
        .arg_count = 3,
        .args = {
            {ARG_INT, 0, false},
            {ARG_BUFFER, 0, false},
            {ARG_INT, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_write] = {
        .func = sys_write,
        .name = "write",
        .arg_count = 3,
        .args = {
            {ARG_INT, 0, false},
            {ARG_BUFFER, 0, false},
            {ARG_INT, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    // 内存管理类
    [SYS_sbrk] = {
        .func = sys_sbrk,
        .name = "sbrk",
        .arg_count = 1,
        .args = {
            {ARG_INT, 0, false}
        },
        .need_proc = true,
        .min_privilege = 0
    },

    [SYS_lseek] = {
        .func = sys_lseek,
        .name = "lseek",
        .arg_count = 3,
        .args = {
            {ARG_INT, 0, false},      // fd
            {ARG_INT, 0, false},      // offset
            {ARG_INT, 0, false}       // whence
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_stat] = {
        .func = sys_stat,
        .name = "stat",
        .arg_count = 2,
        .args = {
            {ARG_STRING, 256, false},  // path
            {ARG_PTR, sizeof(struct stat), false}  // statbuf
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_fstat] = {
        .func = sys_fstat,
        .name = "fstat",
        .arg_count = 2,
        .args = {
            {ARG_INT, 0, false},       // fd
            {ARG_PTR, sizeof(struct stat), false}  // statbuf
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_mkdir] = {
        .func = sys_mkdir,
        .name = "mkdir",
        .arg_count = 2,
        .args = {
            {ARG_STRING, 256, false},  // path
            {ARG_INT, 0, false}        // mode（暂时忽略）
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_chdir] = {
        .func = sys_chdir,
        .name = "chdir",
        .arg_count = 1,
        .args = {
            {ARG_STRING, 256, false}   // path
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_getcwd] = {
        .func = sys_getcwd,
        .name = "getcwd",
        .arg_count = 2,
        .args = {
            {ARG_PTR, 0, false},       // buf
            {ARG_INT, 0, false}        // size
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_link] = {
        .func = sys_link,
        .name = "link",
        .arg_count = 2,
        .args = {
            {ARG_STRING, 256, false},  // oldpath
            {ARG_STRING, 256, false}   // newpath
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_unlink] = {
        .func = sys_unlink,
        .name = "unlink",
        .arg_count = 1,
        .args = {
            {ARG_STRING, 256, false}   // path
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_dup] = {
        .func = sys_dup,
        .name = "dup",
        .arg_count = 1,
        .args = {
            {ARG_INT, 0, false}        // oldfd
        },
        .need_proc = true,
        .min_privilege = 0
    },
    
    [SYS_dup2] = {
        .func = sys_dup2,
        .name = "dup2",
        .arg_count = 2,
        .args = {
            {ARG_INT, 0, false},       // oldfd
            {ARG_INT, 0, false}        // newfd
        },
        .need_proc = true,
        .min_privilege = 0
    },
};

const int syscall_table_size = sizeof(syscall_table) / sizeof(syscall_table[0]);
