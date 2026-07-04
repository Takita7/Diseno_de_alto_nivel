// simt_controller.h – SIMT divergence and reconvergence controller
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
    enum class RecovergenceMode {
        IMMEDIATE,
        DEFERRED,
        SYNC_ONLY
    };

    struct Config {
        RecovergenceMode mode                  = RecovergenceMode::IMMEDIATE;
        bool             enable_divergence_tracking = true;
        uint32_t         max_history_depth     = 8;
    };

    // reset removed – nothing connects it yet; add back in Phase 3
    sc_core::sc_in<bool> clk{"clk"};

    SC_HAS_PROCESS(SIMTController);
    SIMTController(sc_core::sc_module_name name, const Config& config);
    ~SIMTController();

    void     initializeWarp  (WarpID warp_id, uint32_t threads_per_warp);
    void     handleBranch    (WarpID warp_id, bool* thread_conditions);
    void     handleJoin      (WarpID warp_id);
    uint32_t getActiveMask   (WarpID warp_id) const;
    bool     isThreadActive  (WarpID warp_id, ThreadID thread_id) const;

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

    uint32_t divergence_events_ = 0;
    uint32_t wasted_cycles_     = 0;

    void computeActiveMask   (WarpID warp_id, const bool* conditions);
    void pushDivergenceState (WarpID warp_id, uint32_t mask);
    void popDivergenceState  (WarpID warp_id);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_SIMT_CONTROLLER_H