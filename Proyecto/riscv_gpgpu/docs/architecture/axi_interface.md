# GPGPU AXI Interface Definition (Kria KV260/KR260)

**Task**: T050 · **Status**: Defined · **Code mirror**: [driver/src/fpga_regs.h](../../driver/src/fpga_regs.h)

This document defines the hardware/software contract between the ARM PS
(Cortex-A53, Linux userspace driver) and the PL GPGPU on the AMD Kria
KV260/KR260. Any change to this contract must be applied simultaneously to
`driver/src/fpga_regs.h` and to the HLS/RTL top-level ports.

---

## 1. Address map overview

| Region                     | Physical base | Size    | Bus            | Direction |
|----------------------------|---------------|---------|----------------|-----------|
| Control/status registers   | `0xA000_0000` | 4 KiB   | AXI4-Lite slave (PS GP0 → PL) | PS writes/reads |
| FPGA global memory aperture| `0x6000_0000` | 64 MiB  | AXI4 (PL masters → DDR)       | PL reads/writes, PS via `/dev/mem` or DMA proxy |

The global memory aperture holds both **instruction memory** (kernel ELF
`PT_LOAD` segments, loaded at their link-time virtual addresses) and **data
memory** (device buffers allocated by the driver).

## 2. AXI4-Lite register block

* Slave port: `s_axi_ctrl`, 32-bit data, 12-bit address (4 KiB window).
* All registers are 32-bit, little-endian, word-aligned.
* Reserved offsets (`0x1C`–`0xFFC`) read as `0` and ignore writes.

| Offset | Name         | Access | Reset value  | Description |
|--------|--------------|--------|--------------|-------------|
| `0x00` | `ID`         | RO     | `0x4750_5501`| Device identification/version (`"GPU"` + v1). The driver validates this before any other access. |
| `0x04` | `CTRL`       | RW     | `0x0`        | Bit 0 `START` (self-clearing): begin kernel execution. Bit 1 `RESET`: synchronous reset of the compute pipeline, returns `STATUS` to `IDLE`. Bit 2 `IRQ_CLEAR`: acknowledge the done interrupt. |
| `0x08` | `STATUS`     | RO     | `0x0` (IDLE) | `0`=IDLE, `1`=RUNNING, `2`=DONE, `3`=ERROR. |
| `0x0C` | `PC_INIT`    | RW     | `0x0`        | RISC-V entry point PC written by the ELF loader (`e_entry`). Latched when `CTRL.START` is asserted. |
| `0x10` | `GRID_X`     | RW     | `0x1`        | Launch grid dimension X. |
| `0x14` | `GRID_Y`     | RW     | `0x1`        | Launch grid dimension Y. |
| `0x18` | `IRQ_ENABLE` | RW     | `0x0`        | Bit 0: enable level interrupt on `STATUS == DONE`. |

### Execution handshake

1. Driver writes `PC_INIT`, `GRID_X`, `GRID_Y` (T052/T053).
2. Driver writes `CTRL.START = 1`; hardware clears the bit and moves `STATUS`
   to `RUNNING` (must occur within 10 ms — verified by T053).
3. On completion, hardware sets `STATUS = DONE` and, if `IRQ_ENABLE.DONE` is
   set, raises the `irq_done` line (PL→PS IRQ0, mapped to a UIO device).
4. Driver reads results, then writes `CTRL.RESET = 1` to return to `IDLE`.

Any AXI decode error, illegal instruction, or memory fault moves `STATUS` to
`ERROR`; only `CTRL.RESET` leaves that state.

## 3. AXI4 DMA channels (PL masters)

| Port         | Type        | Width | Purpose |
|--------------|-------------|-------|---------|
| `m_axi_imem` | AXI4 master | 64-bit data, 32-bit address | Instruction memory load: fetches kernel code from the global memory aperture where the ELF loader placed the `PT_LOAD` segments (H2D, T052). |
| `m_axi_dmem` | AXI4 master | 64-bit data, 32-bit address | Data memory: kernel loads/stores against device buffers (H2D before launch, D2H after `STATUS == DONE`). |

Host-side transfers into the aperture use `mmap()` of `/dev/mem` at
`0x6000_0000` (or a kernel DMA-proxy character device when cache coherency
management is required). Buffers are 16-byte aligned; the driver implements a
bump allocator over the aperture (see `driver/src/fpga_driver.cpp`).

## 4. Interrupt

| Signal     | Type            | Mapping |
|------------|-----------------|---------|
| `irq_done` | Level, active-high | PL→PS `pl_ps_irq0`, exposed to userspace as `/dev/uioN` ("gpgpu-ctrl"). |

`gpgpuSynchronize()` (T054) either blocks on a `read()` of the UIO device or
polls `STATUS == DONE` with a timeout; both paths are implemented.

## 5. Device tree fragment (reference)

```dts
gpgpu@a0000000 {
    compatible = "generic-uio";
    reg = <0x0 0xa0000000 0x0 0x1000>;
    interrupt-parent = <&gic>;
    interrupts = <0 89 4>;   /* pl_ps_irq0 */
};
```

## 6. Constraints

Clock and reset constraints for the PL implementation live in
[fpga/constraints/kv260_gpgpu.xdc](../../fpga/constraints/kv260_gpgpu.xdc).
