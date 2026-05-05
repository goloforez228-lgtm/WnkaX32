ENTRY(_start)

SECTIONS {
    . = 1M;

    .multiboot : {
        KEEP(*(.multiboot))
    }

    .text : {
        *(.text)
    }

    .rodata : {
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(COMMON)
        *(.bss)
    }

    _kernel_start = ADDR(.multiboot);
    _kernel_end = .;
}