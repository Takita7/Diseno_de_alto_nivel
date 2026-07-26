// top.cpp – GPGPUTop
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

    WarpScheduler::Config sched_config;
    sched_config.num_compute_units   = config_.num_compute_units;
    sched_config.max_warps_per_cu    = config_.max_warps_per_cu;
    sched_config.policy              = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    sched_config.enable_optimization = true;
    sched_config.batch_size          = 4;
    scheduler_ = std::make_unique<WarpScheduler>("scheduler", sched_config);
    scheduler_->clk(system_clock);

    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = config_.shared_mem_size;
    mem_config.l1_cache_size   = config_.l1_cache_size;
    mem_config.l2_cache_size   = config_.l2_cache_size;
    mem_config.cache_line_size = 128;
    mem_config.global_mem_size = 0;
    memory_ = std::make_unique<MemoryHierarchy>("memory", mem_config);
    memory_->clk(system_clock);

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
                              std::vector<Instruction> program,
                              uint32_t warp_id_offset) {
    LOG_INFO("launchKernel: grid=" + std::to_string(grid_x)
             + "x" + std::to_string(grid_y)
             + "  program=" + std::to_string(program.size()) + " instructions"
             + "  warp_offset=" + std::to_string(warp_id_offset));

    kernel_program_       = std::move(program);
    kernel_start_warp_id_ = scheduler_->getNextWarpId();   // capture before submit
    warp_id_offset_       = warp_id_offset;
    total_warps_          = grid_x * grid_y;
    scheduler_->submitKernel(0, grid_x, grid_y);
    kernel_launch_event_.notify();
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
        uint32_t global_tid = (warp_id + warp_id_offset_) * tpw + t;
        ctx.regs[t][0]      = 0;
        ctx.regs[t][1]      = global_tid;
        ctx.regs[t][2]      = 0x10000 + global_tid * 4;
        ctx.regs[t][3]      = warp_id - kernel_start_warp_id_;  // local warp ID (0,1,2,...)
    }
    return ctx;
}

// ── Simulation loop (SC_THREAD) ───────────────────────────────────────────────
//
// Fan-out: each outer iteration tries to dispatch one warp from EVERY CU,
// so all compute units make progress in parallel within each delta cycle.
//
// Barrier coordination: stalled warps from all CUs accumulate in barrier_queue.
// The barrier fires globally when barrier_queue.size() == total_warps_,
// which means every warp in the kernel (across all CUs) has arrived.
// Resumed warps re-execute on their original CU.

void GPGPUTop::simulationProcess() {
    while (true) {
        wait(kernel_launch_event_);
        LOG_INFO("simulationProcess: kernel execution started ("
                 + std::to_string(config_.num_compute_units) + " CU(s))");

        struct StalledWarp {
            WarpID      id;
            uint32_t    cu_id;
            uint32_t    bid;
            WarpContext ctx;
        };
        std::vector<StalledWarp> barrier_queue;

        while (!scheduler_->isComplete() || !barrier_queue.empty()) {

            // ── Fan out: dispatch one warp per CU ─────────────────────────
            for (uint32_t cu_id = 0; cu_id < config_.num_compute_units; ++cu_id) {
                WarpID warp_id = scheduler_->selectWarp(cu_id);
                if (warp_id == WarpScheduler::INVALID_WARP_ID) continue;

                WarpContext ctx = buildWarpContext(warp_id);
                uint32_t    bid = 0;
                compute_units_[cu_id]->executeWarp(ctx, &bid);

                if (ctx.state == WarpState::COMPLETE) {
                    scheduler_->markWarpComplete(cu_id, warp_id);
                } else {   // STALLED at barrier
                    scheduler_->markWarpComplete(cu_id, warp_id); // detach
                    barrier_queue.push_back({warp_id, cu_id, bid, ctx});
                    LOG_DEBUG("simulationProcess: warp " + std::to_string(warp_id)
                              + " (CU " + std::to_string(cu_id)
                              + ") at barrier " + std::to_string(bid));
                }
            }

            // ── Global barrier check ──────────────────────────────────────
            // Fires when ALL warps (across all CUs) are in the barrier_queue.
            if (scheduler_->isComplete() && !barrier_queue.empty()) {
                if (barrier_queue.size() == static_cast<size_t>(total_warps_)) {
                    uint32_t bid = barrier_queue[0].bid;

                    // Clear barrier table in every CU
                    for (auto& cu : compute_units_)
                        cu->clearBarrier(bid);

                    LOG_INFO("simulationProcess: barrier " + std::to_string(bid)
                             + " cleared – resuming "
                             + std::to_string(barrier_queue.size())
                             + " warp(s) across "
                             + std::to_string(config_.num_compute_units) + " CU(s)");

                    // Resume each stalled warp on its original CU
                    std::vector<StalledWarp> next_queue;
                    for (auto& sw : barrier_queue) {
                        uint32_t next_bid = 0;
                        compute_units_[sw.cu_id]->executeWarp(sw.ctx, &next_bid);
                        if (sw.ctx.state == WarpState::STALLED)
                            next_queue.push_back({sw.id, sw.cu_id, next_bid, sw.ctx});
                    }
                    barrier_queue = std::move(next_queue);
                }
            }

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
    // Aggregate across all CUs
    uint64_t total = 0;
    for (const auto& cu : compute_units_)
        total += cu->getTotalInstructions();
    return total;
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


// ── readWord ───────────────────────────────────────────────────────
// Reads one 32-bit word from the memory hierarchy for test verification.
// Does NOT update L1 hit/miss counters — use only after kernel completes.

uint32_t GPGPUTop::readWord(Address addr) const {
    if (!memory_) return 0;
    uint32_t data = 0, latency = 0;
    memory_->loadWord(addr, data, latency);
    return data;
}


uint32_t GPGPUTop::getNextWarpId() const {
    return scheduler_ ? scheduler_->getNextWarpId() : 0;
}

}  // riscv_gpgpu
