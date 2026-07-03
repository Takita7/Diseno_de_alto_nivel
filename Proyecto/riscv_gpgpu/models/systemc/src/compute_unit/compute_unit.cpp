// compute_unit.cpp - Baseline compute unit implementation
//
// Implements the compute unit model with warp execution
// and resource management
//

#include "compute_unit.h"
#include "../common/logging.h"
#include <sstream>

namespace riscv_gpgpu {

ComputeUnit::ComputeUnit(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      unit_id_(config.unit_id),
      total_cycles_(0),
      total_instructions_(0),
      current_executing_warp_(0),
      is_running_(false) {
    
    // Initialize warp states
    warp_states_.resize(config.max_warps, WarpState::IDLE);
    
    // Initialize registers for all warps and threads
    registers_.resize(config.max_warps);
    for (auto& warp_regs : registers_) {
        warp_regs.resize(config.num_threads * 32, 0);  // 32 threads, 32 regs each
    }
    
    // Initialize shared memory
    shared_memory_.resize(config.shared_mem_size, 0);
    
    // Register SC_MODULE processes
    SC_METHOD(clockProcess);
    sensitive << clk.pos();
    
    SC_METHOD(resetProcess);
    sensitive << reset.neg();
    
    LOG_INFO(std::string("ComputeUnit ") + std::to_string(unit_id_) + " initialized");
}

ComputeUnit::~ComputeUnit() {
    LOG_INFO(std::string("ComputeUnit ") + std::to_string(unit_id_) + " destroyed");
}

void ComputeUnit::launchKernel(BlockID block_id, uint32_t grid_x, uint32_t grid_y) {
    std::stringstream ss;
    ss << "Launching kernel block " << block_id << " on CU " << unit_id_
       << " (grid: " << grid_x << "x" << grid_y << ")";
    LOG_INFO(ss.str());
    
    is_running_ = true;
    
    // Initialize first warp (in a real implementation, would initialize multiple warps)
    initializeWarp(0);
    ready_warps_.push(0);
}

WarpState ComputeUnit::getWarpState(WarpID warp_id) const {
    if (warp_id < warp_states_.size()) {
        return warp_states_[warp_id];
    }
    return WarpState::IDLE;
}

void ComputeUnit::step() {
    if (!is_running_) {
        return;
    }
    
    // Execute one warp this cycle
    if (!ready_warps_.empty()) {
        current_executing_warp_ = ready_warps_.front();
        ready_warps_.pop();
        executeInstruction(current_executing_warp_);
        ready_warps_.push(current_executing_warp_);  // Re-queue
    }
    
    total_cycles_++;
    updateWarpState();
}

bool ComputeUnit::isComplete() const {
    // Check if all warps are completed
    for (const auto& state : warp_states_) {
        if (state != WarpState::IDLE && state != WarpState::COMPLETED) {
            return false;
        }
    }
    return true;
}

void ComputeUnit::clockProcess() {
    while (true) {
        wait();  // Wait for clock edge
        if (is_running_) {
            step();
        }
    }
}

void ComputeUnit::resetProcess() {
    // Reset all state
    total_cycles_ = 0;
    total_instructions_ = 0;
    is_running_ = false;
    
    for (auto& state : warp_states_) {
        state = WarpState::IDLE;
    }
    
    for (auto& warp_regs : registers_) {
        std::fill(warp_regs.begin(), warp_regs.end(), 0);
    }
    
    std::fill(shared_memory_.begin(), shared_memory_.end(), 0);
}

void ComputeUnit::initializeWarp(WarpID warp_id) {
    warp_states_[warp_id] = WarpState::READY;
    
    // Initialize registers to zero (in practice, would load from kernel args)
    auto& warp_regs = registers_[warp_id];
    std::fill(warp_regs.begin(), warp_regs.end(), 0);
}

void ComputeUnit::finalizeWarp(WarpID warp_id) {
    warp_states_[warp_id] = WarpState::COMPLETED;
    LOG_INFO(std::string("Warp ") + std::to_string(warp_id) + " completed");
}

void ComputeUnit::scheduleWarp() {
    // Simple round-robin scheduling
    for (size_t i = 0; i < warp_states_.size(); ++i) {
        if (warp_states_[i] == WarpState::READY) {
            current_executing_warp_ = i;
            return;
        }
    }
}

void ComputeUnit::executeInstruction(WarpID warp_id) {
    if (warp_states_[warp_id] != WarpState::RUNNING && 
        warp_states_[warp_id] != WarpState::READY) {
        return;
    }
    
    warp_states_[warp_id] = WarpState::RUNNING;
    total_instructions_++;
    
    // In a real implementation, would:
    // 1. Fetch instruction from instruction memory
    // 2. Decode instruction
    // 3. Execute on ALU or memory subsystem
    // 4. Write back results
    
    // For now, simulate instruction execution
    // Placeholder: assume instruction completes in 1 cycle
}

bool ComputeUnit::checkMemoryDependencies(WarpID warp_id) {
    // Check if memory operations for this warp are complete
    // Placeholder: always return true
    return true;
}

void ComputeUnit::updateWarpState() {
    // Update warp states based on execution progress
    for (size_t i = 0; i < warp_states_.size(); ++i) {
        if (warp_states_[i] == WarpState::RUNNING) {
            // Check if warp should complete or stall
            if (total_cycles_ > 100) {  // Simple completion condition
                finalizeWarp(i);
            }
        }
    }
}

}  // namespace riscv_gpgpu
