/* user/user.ld - 用户程序链接脚本 */
OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = 0x1000;  /* 用户程序从 0x1000 开始 */

  .text : {
    *(.text .text.*)
  }

  .rodata : {
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    PROVIDE(__DATA_BEGIN__ = .);
    *(.data .data.*)
    PROVIDE(__DATA_END__ = .);
  }

  .bss : {
    . = ALIGN(16);
    PROVIDE(__BSS_BEGIN__ = .);
    *(.bss .bss.*)
    PROVIDE(__BSS_END__ = .);
  }

  . = ALIGN(0x1000);
  PROVIDE(end = .);
  
  /* 定义全局指针，指向数据段的中间位置 */
  /* 在 RISC-V 中，gp 通常指向 .sdata 或 .data 段的中间 */
  PROVIDE(__global_pointer$ = __DATA_BEGIN__ + 0x800);
}
