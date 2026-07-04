// compute_unit.cpp – Baseline compute unit implementation
//
#include "compute_unit.h"
#include "../integration/riscv_isa.h"
#include "../simt_controller/simt_controller.h"
#include "../common/logging.h"
#include <sstream>
#include <algorithm>

namespace riscv_gpgpu {

ComputeUnit::ComputeUnit(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      unit_id_(config.unit_id),
      total_cycles_(0),
      total_instructions_(0),
      current_executing_warp_(0),
      is_running_(false)
{
    warp_states_.resize(config.max_warps, WarpState::IDLE);

    registers_.resize(config.max_warps);
    for (auto& warp_regs : registers_) {
        warp_regs.resize(config.num_threads * 32, 0);
    }

    shared_memory_.resize(config.shared_mem_size, 0);

    // SC_METHOD fires on every rising clock edge and must return –
    // no wait() calls allowed inside.
    SC_METHOD(clockProcess);
    sensitive << clk.pos();

    LOG_INFO("ComputeUnit " + std::to_string(unit_id_) + " initialized");
}

ComputeUnit::~ComputeUnit() {
    LOG_INFO("ComputeUnit " + std::to_string(unit_id_) + " destroyed");
}

void ComputeUnit::launchKernel(BlockID block_id, uint32_t grid_x, uint32_t grid_y) {
    std::stringstream ss;
    ss << "Launching kernel block " << block_id
       << " on CU " << unit_id_
       << " (grid: " << grid_x << "x" << grid_y << ")";
    LOG_INFO(ss.str());

    is_running_ = true;
    initializeWarp(0);
    ready_warps_.push(0);
}

WarpState ComputeUnit::getWarpState(WarpID warp_id) const {
    if (warp_id < warp_states_.size()) return warp_states_[warp_id];
    return WarpState::IDLE;
}

void ComputeUnit::step() {
    if (!is_running_) return;

    if (!ready_warps_.empty()) {
        current_executing_warp_ = ready_warps_.front();
        ready_warps_.pop();
        executeInstruction(current_executing_warp_);
        ready_warps_.push(current_executing_warp_);
    }

    total_cycles_++;
    updateWarpState();
}

bool ComputeUnit::isComplete() const {
    for (const auto& state : warp_states_) {
        if (state != WarpState::IDLE && state != WarpState::COMPLETE) {
            return false;
    return true;
}

// SC_METHOD – fires on clk.pos(), must return immediately
void ComputeUnit::clockProcess() {
    if (is_running_) {
        step();
    }
}

// resetProcess removed – reset port no longer exists in Phase 0.
// Reset logic (clear cycles, instructions, warp states, registers,
// shared memory) will be triggered explicitly in Phase 4 when a
// proper reset mechanism is designed.

void ComputeUnit::initializeWarp(WarpID warp_id) {
    warp_states_[warp_id] = WarpState::READY;
    auto& warp_regs = registers_[warp_id];
    std::fill(warp_regs.begin(), warp_regs.end(), 0);
}

void ComputeUnit::finalizeWarp(WarpID warp_id) {
    warp_states_[warp_id] = WarpState::COMPLETE;   // was COMPLETED
    LOG_INFO("Warp " + std::to_string(warp_id) + " completed");
}

void ComputeUnit::scheduleWarp() {
    for (size_t i = 0; i < warp_states_.size(); ++i) {
        if (warp_states_[i] == WarpState::READY) {
            current_executing_warp_ = static_cast<WarpID>(i);
            return;
        }
    }
}

void ComputeUnit::executeInstruction(WarpID warp_id) {
    if (warp_states_[warp_id] != WarpState::RUNNING &&
        warp_states_[warp_id] != WarpState::READY) return;

    warp_states_[warp_id] = WarpState::RUNNING;
    total_instructions_++;
}

bool ComputeUnit::checkMemoryDependencies(WarpID /*warp_id*/) {
    return true;
}

void ComputeUnit::updateWarpState() {
    for (size_t i = 0; i < warp_states_.size(); ++i) {
        if (warp_states_[i] == WarpState::RUNNING) {
            if (total_cycles_ > 100) {
                finalizeWarp(static_cast<WarpID>(i));
            }
        }
    }

    if (branch_handled && simt_) {
        simt_->handleBranch(0, branch_conditions.data());
    }

    ctx.rf = ctx.lane_rf[active_lanes.front()];
    ctx.rf[0] = 0;
    ctx.pc = ctx.lane_pc[active_lanes.front()];
    total_instructions_ += static_cast<InstructionCount>(active_lanes.size());

    bool all_done = true;
    for (bool halted : ctx.lane_halted) {
        if (!halted) {
            all_done = false;
            break;
        }
    }
    if (all_done) {
        ctx.halted = true;
        ctx.state = WarpState::COMPLETED;
        if (simt_) simt_->handleJoin(0);
    }
}

void ComputeUnit::executeProcess() {
    // Phase 4: TLM-based execution loop will go here
}

}  // namespace riscv_gpgpu
