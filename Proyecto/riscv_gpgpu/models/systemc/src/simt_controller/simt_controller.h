// simt_controller.h – SIMT divergence and reconvergence controller
//
// Phase 3 adds: hasPendingDivergence() helper for compute unit queries
//

#ifndef RISCV_GPGPU_SIMT_CONTROLLER_H
#define RISCV_GPGPU_SIMT_CONTROLLER_H

#include <systemc>
#include <vector>
#include <stack>
#include "../common/types.h"

namespace riscv_gpgpu {

class SIMTController : public sc_core::sc_module {
public:
    enum class RecovergenceMode { IMMEDIATE, DEFERRED, SYNC_ONLY };

    struct Config {
        RecovergenceMode mode                   = RecovergenceMode::IMMEDIATE;
        bool             enable_divergence_tracking = true;
        uint32_t         max_history_depth      = 8;
    };

    sc_core::sc_in<bool> clk{"clk"};

    SC_HAS_PROCESS(SIMTController);
    SIMTController(sc_core::sc_module_name name, const Config& config);
    ~SIMTController();

    void     initializeWarp  (WarpID warp_id, uint32_t threads_per_warp);

    // Branch: splits active mask into taken / not-taken paths.
    // thread_conditions[i] = true  → thread i takes the branch.
    // Only threads currently in active_mask are considered.
    void     handleBranch    (WarpID warp_id, bool* thread_conditions);

    // Join: pops the not-taken mask and merges threads back.
    // Call when the PC reaches the immediate post-dominator.
    void     handleJoin      (WarpID warp_id);

    uint32_t getActiveMask   (WarpID warp_id) const;
    bool     isThreadActive  (WarpID warp_id, ThreadID thread_id) const;

    // True when the IPDOM stack for this warp is non-empty
    bool     hasPendingDivergence(WarpID warp_id) const;

    // Statistics
    uint32_t getTotalDivergenceEvents() const { return divergence_events_; }
    uint32_t getTotalWastedCycles()     const { return wasted_cycles_;     }

private:
    struct DivergenceStack {
        std::stack<uint32_t>  pc_stack;     // IPDOM PC (placeholder until Phase 4)
        std::stack<uint32_t>  mask_stack;   // not-taken masks awaiting reconvergence
        std::vector<uint32_t> thread_masks; // per-thread condition snapshot
    };

    Config config_;

    std::vector<DivergenceStack> divergence_stacks_;
    std::vector<uint32_t>        active_masks_;
    std::vector<uint32_t>        threads_per_warp_;

    uint32_t divergence_events_ = 0;
    uint32_t wasted_cycles_     = 0;

    void computeActiveMask   (WarpID warp_id, const bool* conditions);
    void pushDivergenceState (WarpID warp_id, uint32_t not_taken_mask);
    void popDivergenceState  (WarpID warp_id);

    // Ensure internal vectors are large enough for warp_id
    void ensureWarpExists    (WarpID warp_id);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_SIMT_CONTROLLER_H