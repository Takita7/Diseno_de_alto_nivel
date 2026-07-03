// simt_controller.h - SIMT divergence and reconvergence controller
//
// Manages thread divergence, active masks, and reconvergence
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
        IMMEDIATE,      // Reconverge immediately at join point
        DEFERRED,       // Defer reconvergence until needed
        SYNC_ONLY       // Only reconverge at explicit barriers
    };

    struct Config {
        RecovergenceMode mode;
        bool enable_divergence_tracking;
        uint32_t max_history_depth;
    };

    // Ports
    sc_core::sc_in<bool> clk{"clk"};
    sc_core::sc_in<bool> reset{"reset"};

    SIMTController(sc_core::sc_module_name name, const Config& config);
    ~SIMTController();

    // Public interface
    void initializeWarp(WarpID warp_id, uint32_t threads_per_warp);
    
    // Branch handling
    void handleBranch(WarpID warp_id, bool* thread_conditions);
    void handleJoin(WarpID warp_id);
    
    // Get warp execution state
    uint32_t getActiveMask(WarpID warp_id) const;
    bool isThreadActive(WarpID warp_id, ThreadID thread_id) const;
    
    // Statistics
    uint32_t getTotalDivergenceEvents() const { return divergence_events_; }
    uint32_t getTotalWastedCycles() const { return wasted_cycles_; }

private:
    struct DivergenceStack {
        std::stack<uint32_t> pc_stack;      // Program counter stack
        std::stack<uint32_t> mask_stack;    // Active mask stack
        std::vector<uint32_t> thread_masks; // Per-thread masks
    };

    Config config_;
    
    // Per-warp divergence tracking
    std::vector<DivergenceStack> divergence_stacks_;
    std::vector<uint32_t> active_masks_;
    std::vector<uint32_t> threads_per_warp_;
    
    // Statistics
    uint32_t divergence_events_;
    uint32_t wasted_cycles_;
    
    // Helper methods
    void computeActiveMask(WarpID warp_id, const bool* conditions);
    void pushDivergenceState(WarpID warp_id, uint32_t mask);
    void popDivergenceState(WarpID warp_id);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_SIMT_CONTROLLER_H
