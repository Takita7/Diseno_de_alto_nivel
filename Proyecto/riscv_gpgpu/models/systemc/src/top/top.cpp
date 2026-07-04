// top.cpp – GPGPUTop module implementation
//

#include "top.h"
#include "../scheduler/warp_scheduler.h"
#include "../memory/memory_hierarchy.h"
#include "../compute_unit/compute_unit.h"
#include "../common/logging.h"
#include "../common/platform.h"

namespace riscv_gpgpu {

GPGPUTop::GPGPUTop(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      system_clock("system_clock",
                   sc_core::sc_time(GPGPU_CLOCK_PERIOD_NS, sc_core::SC_NS))
{
    LOG_INFO("Initializing GPGPU Top Module");

    // ── Warp Scheduler ────────────────────────────────────────────────────────
    WarpScheduler::Config sched_config;
    sched_config.num_compute_units  = config_.num_compute_units;
    sched_config.max_warps_per_cu   = config_.max_warps_per_cu;
    sched_config.policy             = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    sched_config.enable_optimization = true;
    sched_config.batch_size         = 4;

    scheduler_ = std::make_unique<WarpScheduler>("scheduler", sched_config);
    scheduler_->clk(system_clock);

    // ── Memory Hierarchy ──────────────────────────────────────────────────────
    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = config_.shared_mem_size;
    mem_config.l1_cache_size   = config_.l1_cache_size;
    mem_config.l2_cache_size   = config_.l2_cache_size;
    mem_config.cache_line_size = 128;
    mem_config.global_mem_size = 0;  // unlimited

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

    LOG_INFO("GPGPU Top Module initialized with "
             + std::to_string(config_.num_compute_units) + " compute unit(s)");
}

GPGPUTop::~GPGPUTop() {
    LOG_INFO("GPGPU Top Module destroyed");
}

// ── Kernel launch ─────────────────────────────────────────────────────────────

void GPGPUTop::launchKernel(uint32_t grid_x, uint32_t grid_y) {
    LOG_INFO("Launching kernel: grid=" + std::to_string(grid_x)
             + "x" + std::to_string(grid_y));
    scheduler_->submitKernel(0, grid_x, grid_y);

    // Phase 5: replace this stub with proper fan-out across all CUs.
    if (!compute_units_.empty()) {
        compute_units_[0]->launchKernel(0, grid_x, grid_y);
    }
}

bool GPGPUTop::isKernelComplete() const {
    if (compute_units_.empty()) return true;
    return compute_units_[0]->isComplete();
}

// ── Statistics ────────────────────────────────────────────────────────────────

uint64_t GPGPUTop::getTotalCycles() const {
    if (compute_units_.empty()) return 0;
    return compute_units_[0]->getTotalCycles();
}

uint64_t GPGPUTop::getTotalInstructions() const {
    if (compute_units_.empty()) return 0;
    return compute_units_[0]->getTotalInstructions();
}

uint32_t GPGPUTop::getL1CacheHits() const {
    if (!memory_) return 0;
    return memory_->getL1CacheHits();
}

uint32_t GPGPUTop::getL1CacheMisses() const {
    if (!memory_) return 0;
    return memory_->getL1CacheMisses();
}

uint32_t GPGPUTop::getDivergenceEvents() const {
    // Phase 5: collect from each ComputeUnit's SIMTController.
    return 0;
}

// ── Simulation process ────────────────────────────────────────────────────────

void GPGPUTop::simulationProcess() {
    // Placeholder – registered as SC_THREAD in Phase 5.
    while (true) {
        wait();
    }
}

}  // namespace riscv_gpgpu