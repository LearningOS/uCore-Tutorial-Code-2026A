OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
    . = 0x1000;
    .text : { KEEP(*(.text.entry)) *(.text .text.*) }
    . = ALIGN(16);
    .rodata : { *(.rodata .rodata.* .srodata .srodata.*) }
    . = ALIGN(16);
    /* The existing uCore loader consumes a raw image, not ELF headers.
       Materialize zero-initialized storage in the image so it maps BSS too. */
    .data : {
        *(.data .data.* .sdata .sdata.*)
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.* .sbss .sbss.* COMMON)
        __bss_end = .;
        BYTE(0)
    }
    /DISCARD/ : { *(.eh_frame .eh_frame.* .comment) }
}
