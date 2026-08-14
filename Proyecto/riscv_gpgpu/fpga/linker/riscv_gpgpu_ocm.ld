/* riscv_gpgpu_ocm.ld - Links kernel into Zynq OCM at 0xFFFC0000 (256 KB PS SRAM).
 *
 * OCM is NOT Linux DDR → /dev/mem access is allowed even with CONFIG_STRICT_DEVMEM.
 * FPGA AXI masters on S_AXI_HPC0_FPD can address OCM in the PS memory map.
 * No memmap= kernel parameter required.
 *
 * LUI-based global-data references compiled here point to 0xFFFC0000+ (OCM),
 * which is accessible from both ARM (via /dev/mem) and FPGA (via HPC0 AXI).
 *
 * Layout (lower 128 KB only, OCM bank 0-1 - safe on KV260):
 *   0xFFFC0000 .. 0xFFFC3FFF : .text + .data  (16 KB max)
 *   0xFFFC4000 .. 0xFFFDFFFF : initial_regs   (via fpga_rodinia_bench)
 */

ENTRY(0)

MEMORY {
    OCM (rwx) : ORIGIN = 0xFFFC0000, LENGTH = 16K
}

SECTIONS {
    . = ORIGIN(OCM);

    .text : {
        KEEP(*(.text .text.*))
    } > OCM

    . = ALIGN(4);

    .rodata : {
        *(.rodata .rodata.*)
    } > OCM

    . = ALIGN(4);

    .data : {
        *(.data .data.*)
    } > OCM

    . = ALIGN(4);

    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    } > OCM

    . = ALIGN(4);
    _kernel_end = .;
}
