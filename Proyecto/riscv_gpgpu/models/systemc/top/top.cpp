// top.cpp - GPGPUTop implementation
//
// Wires together ComputeUnits, WarpScheduler, SIMTController, and
// MemoryHierarchy into the top-level simulation module.
//

#include "top.h"
#include "../compute_unit/compute_unit.h"
#include "../scheduler/warp_scheduler.h"
#include "../memory/memory_hierarchy.h"
#include "../simt_controller/simt_controller.h"
#include "../common/logging.h"

#include <sstream>

namespace riscv_gpgpu {

GPGPUTop::GPGPUTop(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config) {

    // ── Memory hierarchy ──────────────────────────────────────────────────────
    MemoryHierarchy::Config mem_cfg;
    mem_cfg.shared_mem_size = config.shared_mem_size;
    mem_cfg.global_mem_size = 0;  // sparse map, size determined at runtime
    mem_cfg.cache_line_size = 64;
    mem_cfg.l1_cache_size   = config.l1_cache_size;
    mem_cfg.l2_cache_size   = config.l2_cache_size;
    memory_ = std::make_unique<MemoryHierarchy>("mem_hierarchy", mem_cfg);

    // ── Warp scheduler ────────────────────────────────────────────────────────
    WarpScheduler::Config sched_cfg;
    sched_cfg.num_compute_units  = config.num_compute_units;
    sched_cfg.max_warps_per_cu   = config.max_warps_per_cu;
    sched_cfg.policy             = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    sched_cfg.enable_optimization = true;
    sched_cfg.batch_size         = 4;
    scheduler_ = std::make_unique<WarpScheduler>("warp_scheduler", sched_cfg);

    // ── SIMT controller ───────────────────────────────────────────────────────
    SIMTController::Config simt_cfg;
    simt_cfg.mode                    = SIMTController::RecovergenceMode::IMMEDIATE;
    simt_cfg.enable_divergence_tracking = true;
    simt_cfg.max_history_depth       = 32;

    // ── Compute units ─────────────────────────────────────────────────────────
    compute_units_.reserve(config.num_compute_units);
    mem_ready_sigs_.reserve(config.num_compute_units);
    mem_req_sigs_.reserve(config.num_compute_units);

    for (uint32_t i = 0; i < config.num_compute_units; ++i) {
        ComputeUnit::Config cu_cfg;
        cu_cfg.unit_id         = i;
        cu_cfg.num_threads     = config.threads_per_warp;
        cu_cfg.threads_per_warp = config.threads_per_warp;
        cu_cfg.max_warps       = config.max_warps_per_cu;
        cu_cfg.shared_mem_size = config.shared_mem_size;
        cu_cfg.max_cycles      = 2000000;

        std::string cu_name = "compute_unit_" + std::to_string(i);
        compute_units_.emplace_back(
            std::make_unique<ComputeUnit>(cu_name.c_str(), cu_cfg));

        // Create per-CU signal stubs
        std::string r_name = "mem_ready_" + std::to_string(i);
        std::string q_name = "mem_req_"   + std::to_string(i);
        mem_ready_sigs_.emplace_back(
            std::make_unique<sc_core::sc_signal<bool>>(r_name.c_str()));
        mem_req_sigs_.emplace_back(
            std::make_unique<sc_core::sc_signal<bool>>(q_name.c_str()));

        auto& cu = compute_units_.back();
        // Bind clk/reset
        cu->clk(clk_sig_);
        cu->reset(reset_sig_);
        // Bind memory handshake ports to stub signals
        cu->memory_ready(*mem_ready_sigs_.back());
        cu->memory_request(*mem_req_sigs_.back());

        // Wire shared memory hierarchy into each CU
        cu->setMemoryHierarchy(memory_.get());
    }

    // ── Connect clk/reset (not strictly necessary for functional sim) ─────────
    // Bind our own ports to internal signals so they are not dangling.
    clk(clk_sig_);
    reset(reset_sig_);

    // Bind memory hierarchy clk/reset
    memory_->clk(clk_sig_);
    memory_->reset(reset_sig_);

    // Bind warp_scheduler clk/reset
    scheduler_->clk(clk_sig_);
    scheduler_->reset(reset_sig_);

    SC_HAS_PROCESS(GPGPUTop);
    SC_THREAD(simulationProcess);
    sensitive << clk.pos();

    LOG_INFO("GPGPUTop initialised with " + std::to_string(config.num_compute_units) + " compute units");
}

GPGPUTop::~GPGPUTop() = default;

// ─── Public interface ─────────────────────────────────────────────────────────

void GPGPUTop::launchKernel(uint32_t grid_x, uint32_t grid_y) {
    LOG_INFO("GPGPUTop::launchKernel grid=" + std::to_string(grid_x) + "x" + std::to_string(grid_y));

    uint32_t block = 0;
    for (auto& cu : compute_units_) {
        cu->launchKernel(block++, grid_x, grid_y);
    }
}

bool GPGPUTop::isKernelComplete() const {
    for (const auto& cu : compute_units_) {
        if (!cu->isComplete()) return false;
    }
    return true;
}

MemoryHierarchy* GPGPUTop::getMemoryHierarchy() { return memory_.get(); }

void GPGPUTop::configureKernel(uint32_t entry_pc,
                               const std::array<uint32_t, 32>& init_regs,
                               uint32_t return_sentinel) {
    for (auto& cu : compute_units_) {
        cu->setEntryPoint(entry_pc);
        cu->setInitialRegisters(init_regs);
        cu->setReturnSentinel(return_sentinel);
    }
}

// ─── Statistics ───────────────────────────────────────────────────────────────

uint64_t GPGPUTop::getTotalCycles() const {
    uint64_t c = 0;
    for (const auto& cu : compute_units_) c += cu->getTotalCycles();
    return c;
}

uint64_t GPGPUTop::getTotalInstructions() const {
    uint64_t n = 0;
    for (const auto& cu : compute_units_) n += cu->getTotalInstructions();
    return n;
}

uint32_t GPGPUTop::getL1CacheHits()   const { return memory_ ? (uint32_t)memory_->getL1CacheHits()   : 0; }
uint32_t GPGPUTop::getL1CacheMisses() const { return memory_ ? (uint32_t)memory_->getL1CacheMisses() : 0; }
uint32_t GPGPUTop::getDivergenceEvents() const { return 0; }  // TODO: wire SIMTController

// ─── SC_THREAD process ────────────────────────────────────────────────────────

void GPGPUTop::simulationProcess() {
    while (true) {
        wait();
        // Advance all CUs one step per clock edge
        for (auto& cu : compute_units_) {
            cu->step();
        }
    }
}

}  // namespace riscv_gpgpu
