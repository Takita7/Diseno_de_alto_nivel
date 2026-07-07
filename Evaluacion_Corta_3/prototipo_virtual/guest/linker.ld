/*
 * linker.ld
 *
 * Ubica el código bare-metal y la imagen embebida en direcciones físicas
 * fijas dentro de la RAM de gem5 (system.mem_ranges en gem5/tlm_soc.py).
 *
 * AJUSTAR RAM_ENTRY si su machine-type/CPU model de gem5 espera otro punto
 * de entrada para binarios --kernel bare-metal (revisar el log de arranque
 * de gem5.opt / la documentación de VExpress_GEM5_V1).
 */

ENTRY(_start)

RAM_ENTRY   = 0x00100000;   /* punto de entrada típico para --kernel bare-metal en gem5 ARM */
IMG_INPUT   = 0x01000000;   /* debe coincidir con IMG_INPUT_ADDR en accel_test.c */

SECTIONS
{
    . = RAM_ENTRY;

    .text : {
        *(.text.start)
        *(.text*)
    }

    .rodata : { *(.rodata*) }
    .data   : { *(.data*)   }

    . = IMG_INPUT;
    .img_input : {
        *(.img_input)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
