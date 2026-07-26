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

// DDR4 has meaningfully higher per-access latency than on-chip BRAM/URAM;
// a modest outstanding-transaction depth hides some of that without
// over-provisioning AXI ID tracking hardware. PROVISIONAL: not validated
// against a real Vitis HLS C-synthesis/co-simulation run (vitis_hls is not
// available in the environment this was authored in) - revisit once real
// resource/latency reports exist (docs/hls/interfaces.md SS5 step 5).
#define RISCV_GPGPU_MAXI_NUM_READ_OUTSTANDING  4
#define RISCV_GPGPU_MAXI_NUM_WRITE_OUTSTANDING 2

#endif  // RISCV_GPGPU_HLS_CONFIG_KV260_H
