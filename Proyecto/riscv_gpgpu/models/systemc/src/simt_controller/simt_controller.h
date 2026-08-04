// simt_controller.h – SIMT divergence, reconvergence and barrier controller
//
//

#ifndef RISCV_GPGPU_SIMT_CONTROLLER_H
#define RISCV_GPGPU_SIMT_CONTROLLER_H

#include <systemc>
#include <vector>
#include <stack>
#include <map>
#include <set>
#include "../common/types.h"

namespace riscv_gpgpu {

class SIMTController : public sc_core::sc_module {
public:
    enum class RecovergenceMode { IMMEDIATE, DEFERRED, SYNC_ONLY };

    struct Config {
        RecovergenceMode mode                    = RecovergenceMode::IMMEDIATE;
        bool             enable_divergence_tracking = true;
        uint32_t         max_history_depth       = 8;
    };

    sc_core::sc_in<bool> clk{"clk"};

    SC_HAS_PROCESS(SIMTController);
    SIMTController(sc_core::sc_module_name name, const Config& config);
    ~SIMTController();

    // ── Divergence / reconvergence ────────────────────────────────────────────
    void     initializeWarp     (WarpID warp_id, uint32_t threads_per_warp);
    // reconvergence_pc: the IPDOM join-point PC stored on the divergence stack.
    // Pass 0 when unknown (virtual-ISA path uses explicit VJOIN instead).
    void     handleBranch       (WarpID warp_id, bool* thread_conditions,
                                 uint32_t reconvergence_pc = 0);
    void     handleJoin         (WarpID warp_id);
    uint32_t getActiveMask      (WarpID warp_id) const;
    bool     isThreadActive     (WarpID warp_id, ThreadID thread_id) const;
    bool     hasPendingDivergence(WarpID warp_id) const;
    // Returns the reconvergence PC at the top of the divergence stack (0 if empty).
    uint32_t getReconvergencePC (WarpID warp_id) const;

    // ── Barrier synchronization ───────────────────────────────────────────────
    // Call when a warp reaches a BARRIER instruction.
    void threadHitBarrier(WarpID warp_id, uint32_t barrier_id);

    // Returns true when the number of warps that hit barrier_id == total_warps.
    // total_warps is passed by the caller (simulationProcess knows the kernel size).
    bool allWarpsAtBarrier(uint32_t barrier_id, uint32_t total_warps) const;

    // Clears a barrier entry after it fires so the same barrier_id can be reused.
    void clearBarrier(uint32_t barrier_id);

    // Statistics
    uint32_t getTotalDivergenceEvents() const { return divergence_events_; }
    uint32_t getTotalWastedCycles()     const { return wasted_cycles_;     }

private:
    struct DivergenceStack {
        std::stack<uint32_t>  pc_stack;
        std::stack<uint32_t>  mask_stack;
        std::vector<uint32_t> thread_masks;
    };

    Config config_;

    std::vector<DivergenceStack> divergence_stacks_;
    std::vector<uint32_t>        active_masks_;
    std::vector<uint32_t>        threads_per_warp_;

    // barrier_id → set of WarpIDs that have reached that barrier
    std::map<uint32_t, std::set<WarpID>> barrier_table_;

    uint32_t divergence_events_ = 0;
    uint32_t wasted_cycles_     = 0;

    void computeActiveMask   (WarpID warp_id, const bool* conditions);
    void pushDivergenceState (WarpID warp_id, uint32_t not_taken_mask,
                              uint32_t reconvergence_pc = 0);
    void popDivergenceState  (WarpID warp_id);
    void ensureWarpExists    (WarpID warp_id);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_SIMT_CONTROLLER_H