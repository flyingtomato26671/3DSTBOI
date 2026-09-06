ENTRY(SwizzleTexture)
SECTIONS {
    . = 0x2E6200;
    .text : {
        *(.text*)
        *(.rodata*)
    }
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment*)
        *(.note*)
    }
}
