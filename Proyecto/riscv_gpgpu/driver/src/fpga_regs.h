// fpga_regs.h - GPGPU AXI4-Lite register map (single source of truth for code)
//
// Mirrors docs/architecture/axi_interface.md. Any change here must be
// reflected in that document and in the HLS/RTL top-level ports.

#ifndef RISCV_GPGPU_FPGA_REGS_H
#define RISCV_GPGPU_FPGA_REGS_H

#include <cstdint>

namespace riscv_gpgpu {
namespace fpga {

// ── AXI4-Lite control/status register block (PS → PL) ───────────────────────
// Default physical base on Kria KV260/KR260 (PL AXI GP0 window).
constexpr uint64_t kRegBlockPhysBase = 0xA0000000ULL;
constexpr uint32_t kRegBlockSize     = 0x1000;  // 4 KiB window

// Register byte offsets inside the AXI4-Lite window (32-bit registers).
constexpr uint32_t REG_ID         = 0x00;  // RO  device id/version, expect kDeviceId
constexpr uint32_t REG_CTRL       = 0x04;  // RW  bit0=START (self-clearing), bit1=RESET, bit2=IRQ_CLEAR
constexpr uint32_t REG_STATUS     = 0x08;  // RO  execution state, see Status enum
constexpr uint32_t REG_PC_INIT    = 0x0C;  // RW  kernel entry point (RISC-V PC)
constexpr uint32_t REG_GRID_X     = 0x10;  // RW  launch grid dimension X
constexpr uint32_t REG_GRID_Y     = 0x14;  // RW  launch grid dimension Y
constexpr uint32_t REG_IRQ_ENABLE = 0x18;  // RW  bit0=enable "done" interrupt

// REG_ID expected value: 'RGPU' spelled in hex nibbles + version 0x01.
constexpr uint32_t kDeviceId = 0x47505501;  // "GPU" + v1

// CTRL bit fields.
constexpr uint32_t CTRL_START     = 1u << 0;
constexpr uint32_t CTRL_RESET     = 1u << 1;
constexpr uint32_t CTRL_IRQ_CLEAR = 1u << 2;

// STATUS values.
enum class Status : uint32_t {
    IDLE    = 0,
    RUNNING = 1,
    DONE    = 2,
    ERROR   = 3,
};

// IRQ_ENABLE bit fields.
constexpr uint32_t IRQ_ENABLE_DONE = 1u << 0;

// ── FPGA global memory window (PL DDR aperture, AXI4 masters) ───────────────
// Instruction and data memory live in a shared physical aperture that both
// AXI4 masters (instruction fetch DMA and data DMA) address.
constexpr uint64_t kGlobalMemPhysBase = 0x60000000ULL;
constexpr uint64_t kGlobalMemSize     = 64ULL * 1024 * 1024;  // 64 MiB

} // namespace fpga
} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_FPGA_REGS_H
