/*
 * startup.S
 *
 * Arranque mínimo AArch64 para bare-metal bajo gem5:
 *   - Fija el stack pointer
 *   - Salta a _c_entry (accel_test.c)
 *
 * También reserva, en una sección de datos aparte, el espacio donde vive
 * la imagen de entrada, embebida directamente en el ELF con .incbin. Al
 * cargar el binario como --kernel, gem5 la deposita en RAM en la dirección
 * que fije el linker script (IMG_INPUT_ADDR en accel_test.c y linker.ld
 * deben coincidir).
 */

    .section .text.start
    .global _start
_start:
    /* Stack: crece hacia abajo desde el tope de la sección .stack */
    ldr     x0, =_stack_top
    mov     sp, x0

    bl      _c_entry

    /* _c_entry no debería retornar, pero por si acaso: */
1:  wfe
    b       1b

    /* -------------------------------------------------------------- */
    /* Imagen RGB de entrada embebida (ver PROTOTIPO_VIRTUAL_DISENO.md, */
    /* seccion 5). Reemplazar la ruta por la ubicación real del .raw    */
    /* generado con test/scripts/gen_raw.py.                            */
    /* -------------------------------------------------------------- */
    .section .img_input, "a"
    .global img_input_start
img_input_start:
    .incbin "../test/images/input.raw"
img_input_end:

    .section .bss
    .align 4
    .space 0x4000   /* 16 KB de stack, suficiente para este programa */
_stack_top:
