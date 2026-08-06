/* riscv_gpgpu_fpga.ld - Linker script for RISC-V kernels targeting the FPGA DDR aperture.
 *
 * Places .text + .data into the reserved 64 MiB DDR window at 0x60000000
 * so the FPGA's AXI master and the ARM /dev/mem both see the same physical
 * addresses. Reserve the window on the board with:
 *   memmap=64M$0x60000000  (kernel boot parameter)
 */

ENTRY(0)

MEMORY {
    /* 4 MiB kernel region within the 64 MiB FPGA DDR aperture */
    DDR (rwx) : ORIGIN = 0x60000000, LENGTH = 4M
}

SECTIONS {
    . = ORIGIN(DDR);

    .text : {
        KEEP(*(.text .text.*))
    } > DDR

    . = ALIGN(4);

    .rodata : {
        *(.rodata .rodata.*)
    } > DDR

    . = ALIGN(4);

    .data : {
        *(.data .data.*)
    } > DDR

    . = ALIGN(4);

    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    } > DDR

    . = ALIGN(4);
    _kernel_end = .;
}
