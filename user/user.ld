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
    *(.data .data.*)
  }

  .bss : {
    . = ALIGN(16);
    *(.bss .bss.*)
  }

  . = ALIGN(0x1000);
  PROVIDE(end = .);
}
