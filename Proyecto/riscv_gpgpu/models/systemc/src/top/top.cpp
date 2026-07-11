// top.cpp – GPGPUTop Phase 6
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

        // Phase 6: wire real memory hierarchy into each compute unit
        cu->setMemory(memory_.get());

        compute_units_.push_back(std::move(cu));
    }

    SC_THREAD(simulationProcess);

    LOG_INFO("GPGPU Top Module initialized with "
             + std::to_string(config_.num_compute_units) + " compute unit(s)");
}

GPGPUTop::~GPGPUTop() {
    LOG_INFO("GPGPU Top Module destroyed");
}

// ── Kernel launch ─────────────────────────────────────────────────────────────

void GPGPUTop::launchKernel(uint32_t grid_x, uint32_t grid_y,
                              std::vector<Instruction> program) {
    LOG_INFO("launchKernel: grid=" + std::to_string(grid_x)
             + "x" + std::to_string(grid_y)
             + "  program=" + std::to_string(program.size()) + " instructions");

    kernel_program_ = std::move(program);
    scheduler_->submitKernel(0, grid_x, grid_y);
    kernel_launch_event_.notify();
}

// ── Context builder ───────────────────────────────────────────────────────────
// General-purpose register setup:
//   r0[t] = 0                       (zero register)
//   r1[t] = global thread ID        (warp_id * tpw + t)
//   r2[t] = 0x10000 + tid * 4       (unique 4-byte-aligned memory address)
//
// Kernel programs derive computation values from these using ADDI / VMUL etc.

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
        uint32_t global_tid    = warp_id * tpw + t;
        ctx.regs[t][0]         = 0;                         // zero register
        ctx.regs[t][1]         = global_tid;                // thread ID
        ctx.regs[t][2]         = 0x10000 + global_tid * 4; // unique memory address
    }
    return ctx;
}

// ── Simulation loop (SC_THREAD) ───────────────────────────────────────────────

void GPGPUTop::simulationProcess() {
    while (true) {
        wait(kernel_launch_event_);
        LOG_INFO("simulationProcess: kernel execution started");

        while (!scheduler_->isComplete()) {
            WarpID warp_id = scheduler_->selectWarp(0);

            if (warp_id == WarpScheduler::INVALID_WARP_ID) {
                wait(sc_core::SC_ZERO_TIME);
                continue;
            }

            WarpContext ctx = buildWarpContext(warp_id);
            compute_units_[0]->executeWarp(ctx);
            scheduler_->markWarpComplete(0, warp_id);

            wait(sc_core::SC_ZERO_TIME);
        }

        LOG_INFO("simulationProcess: all warps complete");
    }
}

// ── Status and statistics ─────────────────────────────────────────────────────

bool GPGPUTop::isKernelComplete() const {
    return scheduler_->isComplete();
}

uint64_t GPGPUTop::getTotalCycles() const {
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
    uint32_t total = 0;
    for (const auto& cu : compute_units_)
        total += cu->getDivergenceEvents();
    return total;
}

}  // namespace riscv_gpgpu