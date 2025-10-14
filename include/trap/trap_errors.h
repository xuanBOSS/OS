//定义错误码

#ifndef __TRAP_ERRORS_H__
#define __TRAP_ERRORS_H__

// 中断框架错误码
#define TRAP_OK              0    // 成功
#define TRAP_ERR_INVALID_IRQ -1   // 无效的中断号
#define TRAP_ERR_NULL_HANDLER -2  // 空的处理函数
#define TRAP_ERR_ALREADY_REG -3   // 中断已被注册
#define TRAP_ERR_NOT_REG     -4   // 中断未被注册
#define TRAP_ERR_PLIC_FAIL   -5   // PLIC操作失败

#endif
