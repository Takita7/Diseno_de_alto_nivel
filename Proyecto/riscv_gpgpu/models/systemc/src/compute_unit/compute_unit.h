// compute_unit.h – Compute unit model
//
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

class MemoryHierarchy;

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

    SC_HAS_PROCESS(ComputeUnit);
    ComputeUnit(sc_core::sc_module_name name, const Config& config);
    ~ComputeUnit();

    // ── Legacy clock-driven interface ─────────────────────────────────────────
    void      launchKernel (BlockID block_id, uint32_t grid_x, uint32_t grid_y);
    WarpState getWarpState (WarpID warp_id) const;
    void      step         ();
    bool      isComplete   () const;

    // ── Functional execution ──────────────────────────────────────────────────
    // barrier_id_out (optional): if the warp stalls at a BARRIER instruction,
    // the barrier's ID (instr.imm) is written here.  Pass nullptr to ignore.
    void executeWarp(WarpContext& ctx, uint32_t* barrier_id_out = nullptr);

    // ── Phase 6: external memory ──────────────────────────────────────────────
    void setMemory(MemoryHierarchy* mem);

    // ── Phase 10: barrier queries (delegate to simt_ctrl_) ───────────────────
    bool allWarpsAtBarrier(uint32_t barrier_id, uint32_t total_warps) const;
    void clearBarrier     (uint32_t barrier_id);

    // Statistics
    uint32_t         getDivergenceEvents()  const;
    CycleCount       getTotalCycles()       const { return total_cycles_;       }
    InstructionCount getTotalInstructions() const { return total_instructions_; }

private:
    void clockProcess  ();
    void executeProcess();

    void initializeWarp          (WarpID warp_id);
    void finalizeWarp            (WarpID warp_id);
    void scheduleWarp            ();
    void executeInstruction      (WarpID warp_id);
    bool checkMemoryDependencies (WarpID warp_id);
    void updateWarpState         ();

    void executeALU    (WarpContext& ctx, const Instruction& instr, uint32_t mask);
    void executeVector (WarpContext& ctx, const Instruction& instr, uint32_t mask);
    void executeMemOp  (WarpContext& ctx, const Instruction& instr, uint32_t mask);
    void executeBranch (WarpContext& ctx, const Instruction& instr, uint32_t mask);
    void executeJoin   (WarpContext& ctx, const Instruction& instr, uint32_t mask);

    Config        config_;
    ComputeUnitID unit_id_;

    std::vector<WarpState>              warp_states_;
    std::queue<WarpID>                  ready_warps_;
    std::queue<WarpID>                  stalled_warps_;
    std::vector<std::vector<uint32_t>>  registers_;
    std::vector<uint8_t>                shared_memory_;

    std::unique_ptr<SIMTController>  simt_ctrl_;
    std::map<Address, uint32_t>      sim_memory_;
    MemoryHierarchy*                 ext_memory_ = nullptr;

    CycleCount       total_cycles_           = 0;
    InstructionCount total_instructions_     = 0;
    WarpID           current_executing_warp_ = 0;
    bool             is_running_             = false;
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_COMPUTE_UNIT_H