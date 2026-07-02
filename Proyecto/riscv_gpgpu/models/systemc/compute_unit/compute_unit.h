// compute_unit.h - Compute unit with real RV32I(+C) instruction execution
//

#ifndef RISCV_GPGPU_COMPUTE_UNIT_H
#define RISCV_GPGPU_COMPUTE_UNIT_H

#include <systemc>
#include <array>
#include <vector>
#include <queue>
#include <memory>
#include <cstdint>
#include "../common/types.h"
#include "../memory/memory_hierarchy.h"

namespace riscv_gpgpu {

class SIMTController;

// ─── Per-warp execution context ────────────────────────────────────────────────
struct WarpContext {
    std::array<uint32_t, 32> rf{};  // register file (x0 always 0)
    uint32_t pc = 0;                // program counter
    WarpState state = WarpState::IDLE;
    uint32_t stall_cycles = 0;
    bool halted = false;            // set on EBREAK / return via sentinel
    bool multi_lane = false;
    std::vector<std::array<uint32_t, 32>> lane_rf;
    std::vector<uint32_t> lane_pc;
    std::vector<bool> lane_halted;
};

class ComputeUnit : public sc_core::sc_module {
public:
    struct Config {
        uint32_t unit_id;
        uint32_t num_threads;         // total threads (lanes)
        uint32_t threads_per_warp;
        uint32_t max_warps;
        uint32_t shared_mem_size;
        uint32_t max_cycles = 1000000; // safety limit
    };

    // Ports
    sc_core::sc_in<bool> clk{"clk"};
    sc_core::sc_in<bool> reset{"reset"};
    sc_core::sc_out<bool> memory_ready{"memory_ready"};
    sc_core::sc_in<bool>  memory_request{"memory_request"};

    ComputeUnit(sc_core::sc_module_name name, const Config& config);
    ~ComputeUnit();

    // ── Setup (call before sc_start) ──────────────────────────────────────────
    void setMemoryHierarchy(MemoryHierarchy* mem) { mem_ = mem; }
    void setSIMTController(SIMTController* simt) { simt_ = simt; }
    // Sets entry PC and initial register file for warp 0 (thread 0 / lane 0).
    void setEntryPoint(uint32_t pc);
    void setInitialRegisters(const std::array<uint32_t, 32>& regs);
    // Optional: set a return-address sentinel so we detect function return.
    void setReturnSentinel(uint32_t sentinel_pc) { return_sentinel_ = sentinel_pc; }

    // ── Public interface ──────────────────────────────────────────────────────
    void launchKernel(BlockID block_id, uint32_t grid_x, uint32_t grid_y);
    WarpState getWarpState(WarpID warp_id) const;
    void step();         // Execute one cycle (one instruction per warp)
    bool isComplete() const;
    uint32_t getRegister(WarpID wid, uint8_t reg) const;

    // ── Statistics ────────────────────────────────────────────────────────────
    CycleCount        getTotalCycles()       const { return total_cycles_; }
    InstructionCount  getTotalInstructions() const { return total_instructions_; }

private:
    SC_HAS_PROCESS(ComputeUnit);

    void clockProcess();
    void resetProcess();

    // ── Internal helpers ──────────────────────────────────────────────────────
    void executeWarp(WarpID warp_id);
    void decodeAndExecute(WarpContext& ctx, uint32_t raw32, uint32_t raw_pc);
    void executeWarpMultiLane(WarpID warp_id);

    // ── State ─────────────────────────────────────────────────────────────────
    Config               config_;
    MemoryHierarchy*     mem_ = nullptr;
    SIMTController*      simt_ = nullptr;
    std::vector<WarpContext> warps_;

    uint32_t return_sentinel_  = 0xDEADBEEF;
    bool     is_running_       = false;

    CycleCount       total_cycles_      = 0;
    InstructionCount total_instructions_ = 0;
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_COMPUTE_UNIT_H
