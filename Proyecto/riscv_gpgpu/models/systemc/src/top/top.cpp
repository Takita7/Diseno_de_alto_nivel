// top.cpp – GPGPUTop module implementation
//
// Phase 5 changes:
//   - SC_THREAD(simulationProcess) registered in constructor
//   - launchKernel: builds hardcoded SAXPY program, submits to scheduler,
//     fires kernel_launch_event_
//   - simulationProcess: waits for event, loops selectWarp → executeWarp →
//     markWarpComplete until scheduler reports done
//   - isKernelComplete: delegates to scheduler_->isComplete() instead of
//     compute_units_[0]->isComplete()
//   - buildWarpContext: constructs a ready-to-execute WarpContext for SAXPY
//

#include "top.h"
#include "../scheduler/warp_scheduler.h"
#include "../memory/memory_hierarchy.h"
#include "../compute_unit/compute_unit.h"
#include "../common/logging.h"
#include "../common/platform.h"

namespace riscv_gpgpu {

// ── Constructor ───────────────────────────────────────────────────────────────

GPGPUTop::GPGPUTop(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      system_clock("system_clock",
                   sc_core::sc_time(GPGPU_CLOCK_PERIOD_NS, sc_core::SC_NS))
{
    LOG_INFO("Initializing GPGPU Top Module");

    // ── Warp Scheduler ────────────────────────────────────────────────────────
    WarpScheduler::Config sched_config;
    sched_config.num_compute_units   = config_.num_compute_units;
    sched_config.max_warps_per_cu    = config_.max_warps_per_cu;
    sched_config.policy              = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    sched_config.enable_optimization = true;
    sched_config.batch_size          = 4;

    scheduler_ = std::make_unique<WarpScheduler>("scheduler", sched_config);
    scheduler_->clk(system_clock);

    // ── Memory Hierarchy ──────────────────────────────────────────────────────
    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = config_.shared_mem_size;
    mem_config.l1_cache_size   = config_.l1_cache_size;
    mem_config.l2_cache_size   = config_.l2_cache_size;
    mem_config.cache_line_size = 128;
    mem_config.global_mem_size = 0;

    memory_ = std::make_unique<MemoryHierarchy>("memory", mem_config);
    memory_->clk(system_clock);

    // ── Compute Units ─────────────────────────────────────────────────────────
    for (uint32_t i = 0; i < config_.num_compute_units; ++i) {
        ComputeUnit::Config cu_config;
        cu_config.unit_id          = i;
        cu_config.num_threads      = config_.threads_per_warp * config_.max_warps_per_cu;
        cu_config.threads_per_warp = config_.threads_per_warp;
        cu_config.max_warps        = config_.max_warps_per_cu;
        cu_config.shared_mem_size  = config_.shared_mem_size;

        std::string cu_name = "cu_" + std::to_string(i);
        auto cu = std::make_unique<ComputeUnit>(cu_name.c_str(), cu_config);
        cu->clk(system_clock);
        compute_units_.push_back(std::move(cu));
    }

    // Register simulation loop as SC_THREAD (Phase 5)
    SC_THREAD(simulationProcess);

    LOG_INFO("GPGPU Top Module initialized with "
             + std::to_string(config_.num_compute_units) + " compute unit(s)");
}

GPGPUTop::~GPGPUTop() {
    LOG_INFO("GPGPU Top Module destroyed");
}

// ── Kernel launch ─────────────────────────────────────────────────────────────

void GPGPUTop::launchKernel(uint32_t grid_x, uint32_t grid_y) {
    LOG_INFO("launchKernel: grid=" + std::to_string(grid_x)
             + "x" + std::to_string(grid_y));

    // Hardcoded SAXPY: r3[t] = r0[t]*r1[t] + r2[t]
    //   r0 = alpha, r1 = x[t], r2 = y,  r3 = result
    kernel_program_ = {
        makeInstr(Opcode::VMUL, 3, 1, 0, 0),   // r3 = r1 * r0   (x * alpha)
        makeInstr(Opcode::VADD, 3, 3, 2, 0),   // r3 = r3 + r2   (x*alpha + y)
        makeInstr(Opcode::HALT)
    };

    scheduler_->submitKernel(0, grid_x, grid_y);
    kernel_launch_event_.notify();   // wake simulationProcess
}

// ── Context builder ───────────────────────────────────────────────────────────

WarpContext GPGPUTop::buildWarpContext(WarpID warp_id) const {
    WarpContext ctx;
    ctx.warp_id   = warp_id;
    ctx.kernel_id = 0;
    ctx.pc        = 0;
    ctx.state     = WarpState::READY;
    ctx.program   = kernel_program_;

    uint32_t tpw = config_.threads_per_warp;
    ctx.regs.resize(tpw, std::vector<uint32_t>(32, 0));
    ctx.active_mask = (tpw == 32) ? 0xFFFFFFFFu : (1u << tpw) - 1u;

    for (uint32_t t = 0; t < tpw; ++t) {
        ctx.regs[t][0] = 2;                        // r0 = alpha
        ctx.regs[t][1] = warp_id * tpw + t + 1;   // r1 = x[t] (unique per thread)
        ctx.regs[t][2] = 10;                       // r2 = y
    }
    return ctx;
}

// ── Simulation loop (SC_THREAD) ───────────────────────────────────────────────

void GPGPUTop::simulationProcess() {
    while (true) {
        wait(kernel_launch_event_);   // sleep until launchKernel() fires
        LOG_INFO("simulationProcess: kernel execution started");

        while (!scheduler_->isComplete()) {
            // Single-CU dispatch (Phase 5); multi-CU added in Phase 6
            WarpID warp_id = scheduler_->selectWarp(0);

            if (warp_id == WarpScheduler::INVALID_WARP_ID) {
                wait(sc_core::SC_ZERO_TIME);   // yield – no warp ready yet
                continue;
            }

            WarpContext ctx = buildWarpContext(warp_id);
            compute_units_[0]->executeWarp(ctx);
            scheduler_->markWarpComplete(0, warp_id);

            LOG_DEBUG("simulationProcess: warp " + std::to_string(warp_id)
                      + " complete");

            wait(sc_core::SC_ZERO_TIME);   // yield between warps
        }

        LOG_INFO("simulationProcess: all warps complete");
    }
}

// ── Status and statistics ─────────────────────────────────────────────────────

bool GPGPUTop::isKernelComplete() const {
    // Before any kernel: queues are empty → true (correct for Phase 0 test)
    // During execution: returns false
    // After completion: returns true
    return scheduler_->isComplete();
}

uint64_t GPGPUTop::getTotalCycles() const {
    // total_cycles_ is incremented by the clock-driven step() path,
    // which is not used in Phase 5. Returns 0 until Phase 6 unifies paths.
    if (compute_units_.empty()) return 0;
    return compute_units_[0]->getTotalCycles();
}

uint64_t GPGPUTop::getTotalInstructions() const {
    if (compute_units_.empty()) return 0;
    return compute_units_[0]->getTotalInstructions();
}

uint64_t GPGPUTop::getL1CacheHits() const {
    if (!memory_) return 0;
    return memory_->getL1CacheHits();
}

uint64_t GPGPUTop::getL1CacheMisses() const {
    if (!memory_) return 0;
    return memory_->getL1CacheMisses();
}

uint32_t GPGPUTop::getDivergenceEvents() const {
    // Phase 6: aggregate from all CUs' SIMTControllers
    return 0;
}

}  // namespace riscv_gpgpu
