// u55c.h - Board-specific synthesis parameters for the Alveo U55C (T024)
//
// Companion to hls/constraints/u55c.tcl (part/clock). See kv260.h for why
// these are #define macros, not constexpr.
//
// Selected by defining RISCV_GPGPU_BOARD_U55C as a compiler flag (Vitis
// unified/XRT flow's `v++`/`add_files -cflags`, T025/T026 scope).

#ifndef RISCV_GPGPU_HLS_CONFIG_U55C_H
#define RISCV_GPGPU_HLS_CONFIG_U55C_H

// U55C has NO DDR - 16GB HBM2 only, up to 32x256MB pseudo-channels
// (u55c.tcl). 28 bits addresses exactly one 256MB pseudo-channel; this is
// explicitly the OPEN item from docs/hls/interfaces.md SS6: if T025/T026
// binds global_mem across MULTIPLE interleaved pseudo-channels instead of
// one, this needs to widen accordingly. Do not treat 28 as final.
#define RISCV_GPGPU_ADDR_BITS 28

// Same line-per-burst rationale as kv260.h.
#define RISCV_GPGPU_MAXI_MAX_READ_BURST_LEN  32
#define RISCV_GPGPU_MAXI_MAX_WRITE_BURST_LEN 32

// HBM2's ~460GB/s aggregate bandwidth and many parallel pseudo-channels
// support much deeper outstanding-transaction queues than KV260's DDR4
// before diminishing returns - deeper queues here are about keeping HBM's
// available parallelism fed, not hiding single-access latency the way
// KV260's DDR4 tuning is. PROVISIONAL, same caveat as kv260.h: not validated
// against real Vitis HLS synthesis/cosim in this environment.
#define RISCV_GPGPU_MAXI_NUM_READ_OUTSTANDING  16
#define RISCV_GPGPU_MAXI_NUM_WRITE_OUTSTANDING 8

#endif  // RISCV_GPGPU_HLS_CONFIG_U55C_H
