// system_top.cpp – Multi-GPU top-level implementation
//

#include "system_top.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

// ── Constructor ───────────────────────────────────────────────────────────────

SystemTop::SystemTop(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config)
{
    LOG_INFO("SystemTop: initializing " + std::to_string(config_.num_gpus) + " GPU(s)");

    for (uint32_t i = 0; i < config_.num_gpus; ++i) {
        std::string gpu_name = "gpu_" + std::to_string(i);
        gpus_.push_back(
            std::make_unique<GPGPUTop>(gpu_name.c_str(), config_.gpu_config));
    }

    LOG_INFO("SystemTop: all GPUs initialized");
}

SystemTop::~SystemTop() {
    LOG_INFO("SystemTop: destroyed");
}

// ── Kernel launch ─────────────────────────────────────────────────────────────

void SystemTop::launchKernel(uint32_t grid_x, uint32_t grid_y,
                               std::vector<Instruction> program) {
    uint32_t total_warps = grid_x * grid_y;
    uint32_t num_gpus    = static_cast<uint32_t>(gpus_.size());
    uint32_t base        = total_warps / num_gpus;
    uint32_t remainder   = total_warps % num_gpus;

    LOG_INFO("SystemTop: launchKernel total_warps=" + std::to_string(total_warps)
             + "  across " + std::to_string(num_gpus) + " GPU(s)");

    uint32_t offset = 0;
    for (uint32_t i = 0; i < num_gpus; ++i) {
        uint32_t warps_for_gpu = base + (i < remainder ? 1 : 0);

        LOG_INFO("SystemTop: GPU " + std::to_string(i)
                 + " → " + std::to_string(warps_for_gpu)
                 + " warp(s), offset=" + std::to_string(offset));

        if (warps_for_gpu > 0) {
            // Each GPU needs its own copy of the program since GPGPUTop::launchKernel
            // moves it into kernel_program_.  Pass by value → copy for each call.
            gpus_[i]->launchKernel(warps_for_gpu, 1, program, offset);
        }

        offset += warps_for_gpu;
    }
}

// ── Status ────────────────────────────────────────────────────────────────────

bool SystemTop::isComplete() const {
    for (const auto& gpu : gpus_) {
        if (!gpu->isKernelComplete()) return false;
    }
    return true;
}

// ── Aggregated statistics ─────────────────────────────────────────────────────

uint64_t SystemTop::getTotalInstructions() const {
    uint64_t total = 0;
    for (const auto& gpu : gpus_) total += gpu->getTotalInstructions();
    return total;
}

uint64_t SystemTop::getL1CacheHits() const {
    uint64_t total = 0;
    for (const auto& gpu : gpus_) total += gpu->getL1CacheHits();
    return total;
}

uint64_t SystemTop::getL1CacheMisses() const {
    uint64_t total = 0;
    for (const auto& gpu : gpus_) total += gpu->getL1CacheMisses();
    return total;
}

uint32_t SystemTop::getDivergenceEvents() const {
    uint32_t total = 0;
    for (const auto& gpu : gpus_) total += gpu->getDivergenceEvents();
    return total;
}

}  // namespace riscv_gpgpu
