/* kernel/kernel.ld */
OUTPUT_ARCH(riscv)
ENTRY(_start)

BASE_ADDRESS = 0x80000000;

SECTIONS
{
    . = BASE_ADDRESS;
    
    kernel_start = .;

    .text : {
        *(.text .text.*)
        . = ALIGN(16);
    }
    
    etext = .;

    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(16);
    }

    .data : {
        *(.data .data.*)
        . = ALIGN(16);
    }
    
    edata = .;

    .bss : {
        . = ALIGN(16);
        bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        bss_end = .;
    }

    . = ALIGN(4096);
    stack_bottom = .;
    . += 0x4000;        
    stack_end = .;      

    kernel_end = .;
    end = .;            /* 添加end符号 */

    /DISCARD/ : {
        *(.eh_frame .note.GNU-stack)
    }
}
