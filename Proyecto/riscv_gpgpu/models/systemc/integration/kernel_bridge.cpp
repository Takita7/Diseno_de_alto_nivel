// kernel_bridge.cpp - KernelBridge implementation
//
// Orchestrates the complete software → hardware → software flow:
//   1. Read device buffer contents from driver (g_device_buffers via new API).
//   2. Create GPGPUTop (SystemC model).
//   3. Load ELF binary into MemoryHierarchy.
//   4. Copy device buffers into MemoryHierarchy at their device addresses.
//   5. Resolve entry point symbol, set initial registers (kernel args).
//   6. Run GPGPUTop functionally (step-by-step, avoids sc_start re-entry issues).
//   7. Copy results back from MemoryHierarchy into driver device buffers.
//   8. Print performance metrics.
//
// Note: SystemC elaboration (sc_start) is NOT used here because the bridge
// may be called from within an existing simulation context.  The functional
// execute loop directly calls ComputeUnit::step() until isComplete().
//

#include "kernel_bridge.h"
#include "elf_loader.h"
#include "../src/top/top.h"
#include "../src/memory/memory_hierarchy.h"
#include "../src/compute_unit/compute_unit.h"
#include "../src/simt_controller/simt_controller.h"

// Driver API
#include "../../../driver/src/loader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <systemc>

namespace riscv_gpgpu {

KernelBridge::KernelBridge(Config cfg) : cfg_(cfg) {}

bool KernelBridge::runOnHardware(const std::string& kernel_name,
                                  const std::string& binary_path,
                                  const std::vector<uint64_t>& kernel_args,
                                  const std::vector<uint64_t>& device_ptrs) {
    static std::atomic<uint64_t> run_sequence{0};
    const uint64_t run_id = run_sequence.fetch_add(1, std::memory_order_relaxed);
    const std::string run_prefix = "bridge_run_" + std::to_string(run_id);

    std::cout << "[bridge] Starting hardware simulation for kernel '" << kernel_name << "'\n";
    std::cout << "[bridge] ELF binary: " << binary_path << "\n";

    KernelLaunchArgs launch_args;
    const bool have_launch_args = getCurrentLaunchArgs(launch_args);
    if (have_launch_args) {
        std::cout << "[bridge] Driver launch available: "
                  << launch_args.kernel_name
                  << " entry=" << (launch_args.entry_symbol.empty() ? "<unresolved>" : launch_args.entry_symbol)
                  << " grid=" << launch_args.grid_x << "x" << launch_args.grid_y << "x" << launch_args.grid_z
                  << " block=" << launch_args.block_x << "x" << launch_args.block_y << "x" << launch_args.block_z
                  << " shared_mem=" << launch_args.shared_mem_bytes << "\n";
    }

    // ── 1. Build SystemC MemoryHierarchy independently (no sc_start) ──────────
    MemoryHierarchy::Config mem_cfg;
    mem_cfg.shared_mem_size = cfg_.shared_mem_size;
    mem_cfg.global_mem_size = 0;
    mem_cfg.cache_line_size = 64;
    mem_cfg.l1_cache_size   = cfg_.l1_cache_size;
    mem_cfg.l2_cache_size   = cfg_.l2_cache_size;

    MemoryHierarchy mem("bridge_mem", mem_cfg);

    // ── 2. Load ELF into memory ───────────────────────────────────────────────
    ElfLoader elf;
    if (!elf.load(binary_path, mem)) {
        std::cerr << "[bridge] Failed to load ELF: " << binary_path << "\n";
        return false;
    }

    // ── 3. Copy device buffers into SystemC global memory ────────────────────
    for (uint64_t dev_ptr : device_ptrs) {
        std::vector<uint8_t> content;
        if (!getDeviceBufferContent(dev_ptr, content)) {
            std::cerr << "[bridge] Device buffer 0x" << std::hex << dev_ptr
                      << std::dec << " not found\n";
            continue;
        }
        mem.writeBytes(dev_ptr, content.data(), content.size());
        std::cout << "[bridge] Mapped device buffer 0x" << std::hex << dev_ptr
                  << std::dec << " (" << content.size() << " bytes) into sim memory\n";
    }

    // ── 4. Resolve entry point ────────────────────────────────────────────────
    uint32_t entry_pc = 0;
    SymbolEntry sym;
    const std::string& entry_name = (have_launch_args && !launch_args.entry_symbol.empty())
        ? launch_args.entry_symbol
        : kernel_name;

    if (elf.findSymbol(entry_name, sym)) {
        entry_pc = sym.address;
        std::cout << "[bridge] Entry point '" << sym.name
                  << "' at 0x" << std::hex << entry_pc << std::dec << "\n";
    } else {
        entry_pc = elf.getEntryPoint();
        std::cout << "[bridge] Symbol '" << entry_name
                  << "' not found; using ELF e_entry=0x"
                  << std::hex << entry_pc << std::dec << "\n";
    }

    // ── 5. Set up initial register file (RISC-V calling convention) ───────────
    // a0..a7 = x10..x17, n-th kernel_arg maps to a_n.
    std::array<uint32_t, 32> regs{};
    for (size_t i = 0; i < kernel_args.size() && i < 8; ++i) {
        regs[10 + i] = static_cast<uint32_t>(kernel_args[i]);
    }
    // x2 (sp) = stack area; use a scratch region well above device buffers.
    const uint32_t STACK_TOP = 0x20000000u;
    regs[2] = STACK_TOP;
    // x1 (ra) = return sentinel
    const uint32_t RETURN_SENTINEL = 0x00000001u;
    regs[1] = RETURN_SENTINEL;

    std::cout << "[bridge] Initial registers: "
              << "sp=0x" << std::hex << regs[2]
              << " ra=0x" << regs[1] << std::dec
              << " a0..a" << (kernel_args.size()-1) << "=[args]\n";

    // ── 6. Create and run functional compute units ────────────────────────────
    const uint32_t effective_grid_x = have_launch_args ? launch_args.grid_x : 1;
    const uint32_t effective_grid_y = have_launch_args ? launch_args.grid_y : 1;
    const uint32_t effective_grid_z = have_launch_args ? launch_args.grid_z : 1;
    const uint32_t effective_block_x = have_launch_args ? launch_args.block_x : 1;
    const uint32_t effective_block_y = have_launch_args ? launch_args.block_y : 1;
    const uint32_t effective_block_z = have_launch_args ? launch_args.block_z : 1;
    const uint32_t total_blocks = std::max<uint32_t>(1u,
        effective_grid_x * std::max<uint32_t>(1u, effective_grid_y) * std::max<uint32_t>(1u, effective_grid_z));
    const uint32_t live_units = std::max<uint32_t>(1u,
        std::min<uint32_t>(cfg_.num_compute_units == 0 ? 1u : cfg_.num_compute_units, total_blocks));

    struct Worker {
        std::unique_ptr<ComputeUnit> cu;
        std::unique_ptr<SIMTController> simt;
        uint32_t block_id = 0;
        bool active = false;
    };

    auto makeWorker = [&](uint32_t block_id) {
        Worker worker;

        ComputeUnit::Config cu_cfg;
        cu_cfg.unit_id          = block_id;
        cu_cfg.num_threads      = cfg_.threads_per_warp;
        cu_cfg.threads_per_warp = cfg_.threads_per_warp;
        cu_cfg.max_warps        = std::max<uint32_t>(1u, cfg_.max_warps_per_cu);
        cu_cfg.shared_mem_size  = cfg_.shared_mem_size;
        cu_cfg.max_cycles       = cfg_.max_sim_cycles;

        const std::string cu_name = run_prefix + "_cu_" + std::to_string(block_id);
        const std::string simt_name = run_prefix + "_simt_" + std::to_string(block_id);

        worker.simt = std::make_unique<SIMTController>(simt_name.c_str(), SIMTController::Config{});
        worker.cu   = std::make_unique<ComputeUnit>(cu_name.c_str(), cu_cfg);
        worker.cu->setMemoryHierarchy(&mem);
        worker.cu->setSIMTController(worker.simt.get());
        worker.cu->setEntryPoint(entry_pc);
        auto worker_regs = regs;
        worker_regs[30] = block_id;
        worker.cu->setInitialRegisters(worker_regs);
        worker.cu->setReturnSentinel(RETURN_SENTINEL);
        worker.cu->launchKernel(block_id, effective_grid_x, effective_grid_y);

        worker.block_id = block_id;
        worker.active = true;
        return worker;
    };

    std::vector<Worker> workers;
    workers.reserve(live_units);

    uint32_t next_block = 0;
    for (; next_block < total_blocks && workers.size() < live_units; ++next_block) {
        workers.emplace_back(makeWorker(next_block));
    }

    last_cycles_       = 0;
    last_instructions_ = 0;

    uint64_t cycle = 0;
    while (true) {
        bool any_active = false;

        for (auto& worker : workers) {
            if (!worker.active || !worker.cu) {
                continue;
            }

            any_active = true;
            worker.cu->step();
            ++cycle;

            if (cycle % 100000 == 0) {
                std::cout << "[bridge]   ... " << cycle << " functional steps\n";
            }

            if (worker.cu->isComplete()) {
                last_cycles_       += worker.cu->getTotalCycles();
                last_instructions_ += worker.cu->getTotalInstructions();
                worker.active = false;

                if (next_block < total_blocks) {
                    worker = makeWorker(next_block++);
                }
            }
        }

        if (!any_active) {
            break;
        }
    }

    std::cout << "[bridge] Execution complete: "
              << last_cycles_ << " cycles, "
              << last_instructions_ << " instructions\n";

    last_l1_hits_      = static_cast<uint32_t>(mem.getL1CacheHits());
    last_l1_misses_    = static_cast<uint32_t>(mem.getL1CacheMisses());
    last_divergence_events_ = 0;
    for (const auto& worker : workers) {
        if (worker.simt) {
            last_divergence_events_ += worker.simt->getTotalDivergenceEvents();
        }
    }
    last_grid_x_       = effective_grid_x;
    last_grid_y_       = effective_grid_y;
    last_grid_z_       = effective_grid_z;
    last_block_x_      = effective_block_x;
    last_block_y_      = effective_block_y;
    last_block_z_      = effective_block_z;
    last_entry_symbol_ = sym.name.empty() ? entry_name : sym.name;

    // ── 7. Copy results back to driver device buffers ─────────────────────────
    for (uint64_t dev_ptr : device_ptrs) {
        size_t sz = getDeviceBufferSize(dev_ptr);
        if (sz == 0) continue;
        std::vector<uint8_t> result(sz);
        mem.readBytes(dev_ptr, result.data(), sz);
        if (!setDeviceBufferContent(dev_ptr, result)) {
            std::cerr << "[bridge] Failed to write results to device buffer 0x"
                      << std::hex << dev_ptr << std::dec << "\n";
        } else {
            std::cout << "[bridge] Wrote " << sz << " bytes back to device buffer 0x"
                      << std::hex << dev_ptr << std::dec << "\n";
        }
    }

    // ── 8. Print performance metrics ──────────────────────────────────────────
    if (cfg_.print_stats) {
        std::cout << "\n[bridge] ── Performance Metrics ────────────────────────\n";
        std::cout << "[bridge]   Kernel:       " << kernel_name << "\n";
        std::cout << "[bridge]   Entry:        " << last_entry_symbol_ << "\n";
        std::cout << "[bridge]   Grid:         " << last_grid_x_ << "x" << last_grid_y_ << "x" << last_grid_z_ << "\n";
        std::cout << "[bridge]   Block:        " << last_block_x_ << "x" << last_block_y_ << "x" << last_block_z_ << "\n";
        std::cout << "[bridge]   Cycles:       " << last_cycles_ << "\n";
        std::cout << "[bridge]   Instructions: " << last_instructions_ << "\n";
        std::cout << "[bridge]   IPC:          "
                  << (last_cycles_ > 0
                      ? static_cast<double>(last_instructions_) / last_cycles_
                      : 0.0)
                  << "\n";
        std::cout << "[bridge]   L1 hits:      " << last_l1_hits_ << "\n";
        std::cout << "[bridge]   L1 misses:    " << last_l1_misses_ << "\n";
        if (last_l1_hits_ + last_l1_misses_ > 0) {
            double hit_rate = 100.0 * last_l1_hits_ / (last_l1_hits_ + last_l1_misses_);
            std::cout << "[bridge]   L1 hit rate:  " << hit_rate << " %\n";
        }
        std::cout << "[bridge] ────────────────────────────────────────────────\n\n";
    }

    return true;
}

} // namespace riscv_gpgpu
