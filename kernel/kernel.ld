OUTPUT_ARCH( "riscv" )
ENTRY( _entry )

SECTIONS
{
  . = 0x80000000;

  .text : {
    *(.text .text.*)
    
    /* Lab4: trampoline 段必须页对齐并在 .text 段中 */
    . = ALIGN(0x1000);
    PROVIDE(trampoline = .);  /* 提供 trampoline 符号 */
    *(trampsec)
    . = ALIGN(0x1000);
    
    PROVIDE(etext = .);
  }

  .rodata : {
    . = ALIGN(16);
    *(.srodata .srodata.*) /* do not need to distinguish this from .rodata */
    . = ALIGN(16);
    *(.rodata .rodata.*)
  }

  .data : {
    . = ALIGN(16);
    *(.sdata .sdata.*) /* do not need to distinguish this from .data */
    . = ALIGN(16);
    *(.data .data.*)
    PROVIDE(KERNEL_DATA = .);  
  }

  .bss : {
    . = ALIGN(16);
    PROVIDE(bss_start = .);
    *(.sbss .sbss.*) /* do not need to distinguish this from .bss */
    . = ALIGN(16);
    *(.bss .bss.*)
    PROVIDE(bss_end = .);
  }

  PROVIDE(end = .);

  . = ALIGN(0x1000);           
  PROVIDE(ALLOC_BEGIN = .);    
  PROVIDE(ALLOC_END = 0x88000000);  

  .user_init : {
    PROVIDE(user_init_start = .);
    *(.user_init)
    PROVIDE(user_init_end = .);
  }
}
