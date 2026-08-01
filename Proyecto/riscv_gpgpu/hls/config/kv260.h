// kv260.h - Board-specific synthesis parameters for the Kria KV260 (T024)
//
// Companion to hls/constraints/kv260.tcl (part/clock) - this header carries
// the parameters that live in C++/pragma space instead: address width and
// m_axi burst/outstanding-transaction tuning. Selected by defining
// RISCV_GPGPU_BOARD_KV260 as a compiler flag (Vitis HLS project's
// `add_files -cflags "-DRISCV_GPGPU_BOARD_KV260"`, T025 scope) before
// hls_config.h is included.
//
// Values here ARE macros (#define), not constexpr - required so they can be
// substituted directly into #pragma HLS ... argument lists, which the HLS
// front end parses via straight preprocessor text substitution rather than
// full C++ constant-expression evaluation (a named constexpr symbol is not
// reliably accepted there across tool versions).

#ifndef RISCV_GPGPU_HLS_CONFIG_KV260_H
#define RISCV_GPGPU_HLS_CONFIG_KV260_H

// 4GB DDR4 (kv260.tcl), reached via the PS's HP/HPC AXI port - see
// docs/hls/interfaces.md SS4/SS6 for the HP-vs-HPC (coherency) open item,
// which affects the m_axi *binding* (T025/T026) but not this address width.
#define RISCV_GPGPU_ADDR_BITS 32

// One cache line per burst (WORDS_PER_LINE, hls_config.h) - kept as a literal
// here rather than referencing the symbol; see the static_assert in
// memory_pipeline.cpp that catches the two drifting apart.
#define RISCV_GPGPU_MAXI_MAX_READ_BURST_LEN  32
#define RISCV_GPGPU_MAXI_MAX_WRITE_BURST_LEN 32

// 4/2 -> 16/16 DECIDED (docs/hls/interfaces.md SS16.31), not a
// placeholder: real synthesis confirmed the cost is negligible (+4 FF,
// +4 LUT), matching Evaluacion_Corta_3's real, working grayscale_accel.cpp
// reference (num_read_outstanding=16). DDR4 has meaningfully higher
// per-access latency than on-chip BRAM/URAM; a deeper outstanding-
// transaction depth hides more of that. The prior 4/2 values were
// PROVISIONAL/never-validated (vitis_hls wasn't available when they were
// authored) - this session's real synthesis run supersedes that.
#define RISCV_GPGPU_MAXI_NUM_READ_OUTSTANDING  16
#define RISCV_GPGPU_MAXI_NUM_WRITE_OUTSTANDING 16

// docs/hls/interfaces.md SS16.29: 128 matches Zynq UltraScale+'s real
// PL-side AXI HP/HPC port width (AMD's own sizing guidance) - lets Vitis
// HLS pack multiple sequential 32-bit reads into fewer, wider AXI beats.
// The Vivado IP flow's own default is 0 (widening disabled) - confirmed
// via this port's real burst.xml report before this fix, not assumed.
#define RISCV_GPGPU_MAXI_MAX_WIDEN_BITWIDTH 128

#endif  // RISCV_GPGPU_HLS_CONFIG_KV260_H
