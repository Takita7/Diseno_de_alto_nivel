// compute_unit.h – Compute unit model
//
// Phase 4 adds:
//   - SIMTController as an owned child module
//   - executeWarp(): functional execution path (WarpContext in/out)
//   - executeALU / executeVector / executeMemOp helpers
//   - sim_memory_: in-module flat memory for Phase 4 (replaced by TLM in Phase 5)
//

#ifndef RISCV_GPGPU_COMPUTE_UNIT_H
#define RISCV_GPGPU_COMPUTE_UNIT_H

#include <systemc>
#include <vector>
#include <queue>
#include <map>
#include <memory>
#include "../common/types.h"
#include "../simt_controller/simt_controller.h"

namespace riscv_gpgpu {

class ComputeUnit : public sc_core::sc_module {
public:
    struct Config {
        uint32_t unit_id          = 0;
        uint32_t num_threads      = 32;
        uint32_t threads_per_warp = 32;
        uint32_t max_warps        = 4;
        uint32_t shared_mem_size  = 16 * 1024;
    };

    sc_core::sc_in<bool> clk{"clk"};

    // Phase 5: TLM initiator socket will be added here
    // tlm_utils::simple_initiator_socket<ComputeUnit> mem_socket;

    SC_HAS_PROCESS(ComputeUnit);
    ComputeUnit(sc_core::sc_module_name name, const Config& config);
    ~ComputeUnit();

    // ── Legacy clock-driven interface (Phases 0-3) ────────────────────────────
    void      launchKernel (BlockID block_id, uint32_t grid_x, uint32_t grid_y);
    WarpState getWarpState (WarpID warp_id) const;
    void      step         ();
    bool      isComplete   () const;

    // ── Phase 4: functional execution path ────────────────────────────────────
    // Runs all instructions in ctx.program, updating ctx.regs and ctx.state.
    // Increments total_instructions_ for each instruction dispatched.
    void executeWarp(WarpContext& ctx);

    // Statistics
    CycleCount       getTotalCycles()       const { return total_cycles_;       }
    InstructionCount getTotalInstructions() const { return total_instructions_; }

private:
    void clockProcess  ();
    void executeProcess();   // Phase 5 placeholder

    // ── Legacy execution helpers ──────────────────────────────────────────────
    void initializeWarp           (WarpID warp_id);
    void finalizeWarp             (WarpID warp_id);
    void scheduleWarp             ();
    void executeInstruction       (WarpID warp_id);
    bool checkMemoryDependencies  (WarpID warp_id);
    void updateWarpState          ();

    // ── Phase 4 execution helpers ─────────────────────────────────────────────
    void executeALU    (WarpContext& ctx, const Instruction& instr, uint32_t mask);
    void executeVector (WarpContext& ctx, const Instruction& instr, uint32_t mask);
    void executeMemOp  (WarpContext& ctx, const Instruction& instr, uint32_t mask);

    // ── State ─────────────────────────────────────────────────────────────────
    Config        config_;
    ComputeUnitID unit_id_;

    // Legacy warp management
    std::vector<WarpState>              warp_states_;
    std::queue<WarpID>                  ready_warps_;
    std::queue<WarpID>                  stalled_warps_;
    std::vector<std::vector<uint32_t>>  registers_;    // [warp][reg]
    std::vector<uint8_t>                shared_memory_;

    // Phase 4 additions
    std::unique_ptr<SIMTController>  simt_ctrl_;
    std::map<Address, uint32_t>      sim_memory_;  // flat memory; TLM replaces in Phase 5

    CycleCount       total_cycles_           = 0;
    InstructionCount total_instructions_     = 0;
    WarpID           current_executing_warp_ = 0;
    bool             is_running_             = false;
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_COMPUTE_UNIT_H