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
#include <array>
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
        // Maximum step() iterations before isComplete() is forced true.
        // 0 = unlimited (default). Maps to PC_INIT timeout register on FPGA.
        uint32_t max_cycles       = 0;
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

    // ── Binary execution API (mirrors FPGA ARM-driver register writes) ────────
    // setMemoryHierarchy: alias of setMemory; name matches FPGA AXI port.
    void setMemoryHierarchy(MemoryHierarchy* mem);
    // setSIMTController: compatibility shim — bridge owns its own SIMTController
    // for divergence metrics; this stores it but binary mode runs single-thread.
    void setSIMTController(SIMTController* simt);
    // setEntryPoint: initial PC — equivalent to writing the FPGA PC_INIT register.
    void setEntryPoint(uint32_t pc);
    // setInitialRegisters: full 32-register file snapshot for one warp.
    void setInitialRegisters(std::array<uint32_t, 32> regs);
    // setReturnSentinel: PC that signals kernel completion (ra = sentinel on entry).
    void setReturnSentinel(uint32_t sentinel_pc);
    // getRegister: read back a register after execution (e.g. result in a0).
    uint32_t getRegister(uint32_t warp_id, uint32_t reg_id) const;

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
    SIMTController*                  ext_simt_   = nullptr;  // injected by bridge
    std::map<Address, uint32_t>      sim_memory_;
    MemoryHierarchy*                 ext_memory_ = nullptr;

    CycleCount       total_cycles_           = 0;
    InstructionCount total_instructions_     = 0;
    WarpID           current_executing_warp_ = 0;
    bool             is_running_             = false;

    // ── Binary execution mode state ───────────────────────────────────────────
    bool                      binary_mode_             = false;
    uint32_t                  binary_pc_               = 0;
    uint32_t                  binary_return_sentinel_  = 0;
    bool                      binary_halted_           = false;
    std::array<uint32_t, 32>  binary_regs_             = {};
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_COMPUTE_UNIT_H