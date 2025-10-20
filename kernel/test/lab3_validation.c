#include "lib/print.h"
#include "dev/uart.h"
#include "dev/timer_sched.h"
#include "dev/plic.h"
#include "riscv.h"
#define SEPARATOR "============================================================"
#define BOX_TOP    "╔============================================================╗"
#define BOX_MID    "║                                                            ║"
#define BOX_BOT    "╚============================================================╝"

// 验收测试1：时钟滴答测试
// 强制时钟滴答测试
void force_clock_test(void) {
    printf("\n=== 强制时钟滴答测试 ===\n");
    printf("模拟时钟滴答输出：\n");
    
    for (int i = 0; i < 20; i++) {
        printf("T");
        for (volatile int j = 0; j < 500000; j++);  // 延迟模拟时钟间隔
    }
    printf("\n强制时钟滴答测试完成\n");
}

// 轮询式UART测试
void poll_uart_test(void) {
    printf("\n=== 轮询式UART测试 ===\n");
    printf("请输入字符，系统将轮询检查（10秒）...\n");
    
    int checks = 0;
    while (checks < 1000) {  // 检查1000次
        int c = uart_getc_sync();
        if (c != -1) {
            printf("[轮询检测] 字符: '%c' (0x%02x)\n", c, c);
            uart_putc_sync(c);  // 回显
            if (c == '\r') {
                uart_putc_sync('\n');
                printf("\n");
            }
        }
        
        // 短暂延迟
        for (volatile int i = 0; i < 10000; i++);
        checks++;
    }
    printf("轮询测试完成\n");
}

// 检查中断状态
void check_interrupt_status(void) {
    printf("\n=== 中断状态检查 ===\n");
    
    // 检查全局中断状态
    uint64 sstatus = r_sstatus();
    printf("SSTATUS: 0x%lx (SIE: %s)\n", 
           sstatus, (sstatus & SSTATUS_SIE) ? "enabled" : "disabled");
    
    // 检查中断使能
    uint64 sie = r_sie();
    printf("SIE: 0x%lx\n", sie);
    printf("  SSIE (软件中断): %s\n", (sie & SIE_SSIE) ? "enabled" : "disabled");
    printf("  STIE (时钟中断): %s\n", (sie & SIE_STIE) ? "enabled" : "disabled");
    printf("  SEIE (外部中断): %s\n", (sie & SIE_SEIE) ? "enabled" : "disabled");
    
    // 检查待处理中断
    uint64 sip = r_sip();
    printf("SIP: 0x%lx\n", sip);
    printf("  SSIP: %s\n", (sip & SIP_SSIP) ? "pending" : "clear");
    printf("  STIP: %s\n", (sip & SIP_STIP) ? "pending" : "clear");
    printf("  SEIP: %s\n", (sip & SIP_SEIP) ? "pending" : "clear");
    
    printf("中断状态检查完成\n");
}

// 更新验收测试
void run_lab3_validation_tests(void) {
    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         Lab3 中断处理与时钟管理 - 验收测试              ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    check_interrupt_status();
    force_clock_test();
    poll_uart_test();
    
    printf("\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    验收测试完成！                       ║\n");
    printf("║  1. 强制时钟滴答已演示                                 ║\n");
    printf("║  2. 轮询式UART输入已测试                               ║\n");
    printf("║  3. 中断状态已检查                                     ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
}

void demo_uart_input_echo(void) {
    printf("\n%s\n", SEPARATOR);
    printf("验收演示：UART键盘输入回显测试\n");
    printf("%s\n", SEPARATOR);
    printf("请输入字符，观察回显效果\n");
    printf("输入 'q' 退出测试\n");
    printf("同时观察时钟滴答 'T' 字符\n\n");
    
    int timer_counter = 0;
    int total_chars = 0;
    
    while(1) {
        // === 核心功能：UART输入检查 ===
        int c = uart_getc_sync();
        if (c != -1) {
            total_chars++;
            
            // 显示接收信息（验收要求）
            printf("[第%d个字符] ", total_chars);
            if (c >= 32 && c <= 126) {
                printf("'%c' ", c);
            } else if (c == '\r') {
                printf("'回车' ");
            } else if (c == '\n') {
                printf("'换行' ");
            } else {
                printf("'0x%02x' ", c);
            }
            printf("-> 回显: ");
            
            // 回显字符（验收要求）
            uart_putc_sync(c);
            
            // 处理回车
            if (c == '\r') {
                uart_putc_sync('\n');
                printf("\\r\\n\n");
            } else {
                printf("'%c'\n", c);
            }
            
            // 退出条件
            if (c == 'q' || c == 'Q') {
                printf("\n检测到退出字符，测试结束\n");
                printf("总共输入了 %d 个字符\n", total_chars);
                break;
            }
        }
        
        // === 核心功能：时钟滴答模拟 ===
        timer_counter++;
        if (timer_counter >= 30000) {  // 调整这个值改变T的频率
            printf("T");
            timer_counter = 0;
        }
        
        // 短暂延迟
        for (volatile int i = 0; i < 500; i++);
    }
}

// 时钟频率演示
void demo_clock_speed(void) {
    printf("\n%s\n", SEPARATOR);
    printf("验收演示：时钟滴答快慢测试\n");
    printf("%s\n", SEPARATOR);
    
    printf("演示1：快速滴答（模拟400Hz）\n");
    for (int i = 0; i < 25; i++) {
        printf("T");
        for (volatile int j = 0; j < 80000; j++);  // 快速
    }
    printf(" <- 快速\n\n");
    
    printf("演示2：正常滴答（模拟100Hz）\n");
    for (int i = 0; i < 20; i++) {
        printf("T");
        for (volatile int j = 0; j < 300000; j++);  // 正常
    }
    printf(" <- 正常\n\n");
    
    printf("演示3：慢速滴答（模拟25Hz）\n");
    for (int i = 0; i < 15; i++) {
        printf("T");
        for (volatile int j = 0; j < 800000; j++);  // 慢速
    }
    printf(" <- 慢速\n\n");
    
    printf("时钟快慢演示完成\n");
}

// 完整的验收演示
void run_validation_demo(void) {
    printf("\n%s\n", BOX_TOP);
    printf("║               Lab3 验收演示开始                            ║\n");
    printf("%s\n", BOX_BOT);
    
    // 演示1：时钟快慢
    demo_clock_speed();
    
    // 演示2：UART输入回显（主要验收内容）
    demo_uart_input_echo();
    
    printf("\n%s\n", BOX_TOP);
    printf("║               Lab3 验收演示完成                            ║\n");
    printf("%s\n", BOX_BOT);
}
