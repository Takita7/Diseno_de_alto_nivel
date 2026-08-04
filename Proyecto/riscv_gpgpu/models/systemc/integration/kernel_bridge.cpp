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
    // x3 (gp) = THREAD_CTX_BASE — each thread gets its own slot.
    // Slot for thread T is at THREAD_CTX_BASE + T * 64 bytes.
    // Layout: [0]=tid.x [4]=tid.y [8]=tid.z [12]=ctaid.x [16]=ctaid.y [20]=ctaid.z [24]=ntid.x [28]=ntid.y [32]=ntid.z
    //
    // Address constraints for THREAD_CTX_BASE:
    //   > shared_mem_size (0xC000 = 48KB by default)     — avoids loadWord shared path
    //   > ELF code region (~0x10000..0x12000 for lld)    — avoids THREAD_CTX overwriting instructions
    //   < device_buffers (0x10000000)                    — driver allocations start here
    //
    // Safe choice: 0x00200000 (2MB). For 1024 threads × 64 bytes = 64KB → range
    // 0x200000..0x20FFFF, well below device buffers and above ELF code.
    const uint32_t THREAD_CTX_BASE   = 0x00200000u;
    const uint32_t THREAD_CTX_STRIDE = 64u;

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
    // Threads per block: for single-thread kernels this is 1; for SIMT kernels
    // this is block_x * block_y * block_z (each gets its own a4/a5 values).
    const uint32_t threads_per_block = std::max<uint32_t>(1u,
        effective_block_x * std::max<uint32_t>(1u, effective_block_y) * std::max<uint32_t>(1u, effective_block_z));
    const uint32_t total_threads = total_blocks * threads_per_block;
    struct Worker {
        std::unique_ptr<ComputeUnit>    cu;
        std::unique_ptr<SIMTController> simt;
        uint32_t block_id  = 0;
        uint32_t thread_id = 0;
        bool     active    = false;
    };

    // global_thread_id encodes both block and thread: maps (block_id, thread_id)
    // to a4 (block_id) and a5 (thread_id) following RISC-V ABI/GPGPU convention.
    auto makeWorker = [&](uint32_t global_thread_id) {
        Worker worker;
        const uint32_t block_id  = global_thread_id / threads_per_block;
        const uint32_t thread_id = global_thread_id % threads_per_block;

        ComputeUnit::Config cu_cfg;
        cu_cfg.unit_id          = global_thread_id;
        cu_cfg.num_threads      = 1;
        cu_cfg.threads_per_warp = 1;
        cu_cfg.max_warps        = std::max<uint32_t>(1u, cfg_.max_warps_per_cu);
        cu_cfg.shared_mem_size  = cfg_.shared_mem_size;
        cu_cfg.max_cycles       = cfg_.max_sim_cycles;

        const std::string cu_name   = run_prefix + "_cu_"   + std::to_string(global_thread_id);
        const std::string simt_name = run_prefix + "_simt_" + std::to_string(global_thread_id);

        worker.simt = std::make_unique<SIMTController>(simt_name.c_str(), SIMTController::Config{});
        worker.cu   = std::make_unique<ComputeUnit>(cu_name.c_str(), cu_cfg);
        worker.cu->setMemoryHierarchy(&mem);
        worker.cu->setSIMTController(worker.simt.get());
        worker.cu->setEntryPoint(entry_pc);
        auto worker_regs  = regs;
        // x3 (gp) = per-thread THREAD_CTX slot
        const uint32_t ctx_base = THREAD_CTX_BASE + global_thread_id * THREAD_CTX_STRIDE;
        worker_regs[3] = ctx_base;
        worker.cu->setInitialRegisters(worker_regs);

        // Inject thread context into simulation memory
        // Compute tid and ctaid from global_thread_id, effective_block_x/y/z
        const uint32_t threads_per_block_xyz = std::max<uint32_t>(1u,
            effective_block_x * std::max<uint32_t>(1u, effective_block_y)
                              * std::max<uint32_t>(1u, effective_block_z));
        const uint32_t blk_id    = global_thread_id / threads_per_block_xyz;
        const uint32_t thd_id    = global_thread_id % threads_per_block_xyz;
        const uint32_t tid_x   = thd_id % std::max<uint32_t>(1u, effective_block_x);
        const uint32_t tid_y   = (thd_id / std::max<uint32_t>(1u, effective_block_x)) % std::max<uint32_t>(1u, effective_block_y);
        const uint32_t tid_z   = thd_id / (std::max<uint32_t>(1u, effective_block_x) * std::max<uint32_t>(1u, effective_block_y));
        const uint32_t ctaid_x = blk_id % std::max<uint32_t>(1u, effective_grid_x);
        const uint32_t ctaid_y = (blk_id / std::max<uint32_t>(1u, effective_grid_x)) % std::max<uint32_t>(1u, effective_grid_y);
        const uint32_t ctaid_z = blk_id / (std::max<uint32_t>(1u, effective_grid_x) * std::max<uint32_t>(1u, effective_grid_y));
        const uint32_t thread_ctx[9] = {
            tid_x, tid_y, tid_z,
            ctaid_x, ctaid_y, ctaid_z,
            std::max<uint32_t>(1u, effective_block_x),
            std::max<uint32_t>(1u, effective_block_y),
            std::max<uint32_t>(1u, effective_block_z)
        };
        mem.writeBytes(ctx_base, reinterpret_cast<const uint8_t*>(thread_ctx), 9 * 4);
        worker.cu->setReturnSentinel(RETURN_SENTINEL);
        worker.cu->launchKernel(block_id, effective_grid_x, effective_grid_y);

        worker.block_id  = block_id;
        worker.thread_id = thread_id;
        worker.active    = true;
        return worker;
    };

    last_cycles_            = 0;
    last_instructions_      = 0;
    last_divergence_events_ = 0;

    // ── Choose execution strategy ─────────────────────────────────────────────
    // When threads_per_warp > 1, run all threads in each warp in lockstep.
    // After every instruction step, compare PCs across the warp: the first
    // cycle where threads disagree marks a SIMT divergence event (mirrors
    // what the FPGA SIMT controller tracks via handleBranch).
    //
    // When threads_per_warp == 1 (scalar kernels), fall back to the original
    // independent single-thread-per-worker loop.

    const uint32_t warp_size = (cfg_.threads_per_warp > 1 && threads_per_block > 1)
                               ? std::min(cfg_.threads_per_warp, threads_per_block)
                               : 1u;

    if (warp_size > 1) {
        // ── SIMT warp-level lockstep path ─────────────────────────────────────
        struct WarpGroup {
            std::vector<Worker> threads;
            bool active             = true;
            bool currently_diverged = false;
        };

        const uint32_t total_warps = (total_threads + warp_size - 1) / warp_size;
        const uint32_t concurrent_warps = std::max<uint32_t>(1u,
            std::min<uint32_t>(cfg_.num_compute_units == 0 ? 1u : cfg_.num_compute_units,
                               total_warps));

        auto makeWarpGroup = [&](uint32_t warp_idx) {
            WarpGroup wg;
            uint32_t base = warp_idx * warp_size;
            for (uint32_t t = 0; t < warp_size && base + t < total_threads; ++t)
                wg.threads.emplace_back(makeWorker(base + t));
            return wg;
        };

        std::vector<WarpGroup> warp_groups;
        warp_groups.reserve(concurrent_warps);
        uint32_t next_warp = 0;
        for (; next_warp < total_warps && warp_groups.size() < concurrent_warps; ++next_warp)
            warp_groups.emplace_back(makeWarpGroup(next_warp));

        uint64_t cycle = 0;
        while (true) {
            bool any_active = false;
            for (auto& wg : warp_groups) {
                if (!wg.active) continue;
                any_active = true;

                // Step all non-complete threads in this warp simultaneously.
                for (auto& w : wg.threads)
                    if (w.active && w.cu && !w.cu->isComplete())
                        w.cu->step();
                ++cycle;

                if (cycle % 100000 == 0)
                    std::cout << "[bridge]   ... " << cycle << " functional warp-steps\n";

                // ── SIMT divergence detection: compare PCs across warp ────────
                // A transition from uniform to non-uniform PC = one divergence event.
                uint32_t ref_pc      = UINT32_MAX;
                bool     pcs_uniform = true;
                for (auto& w : wg.threads) {
                    if (!w.active || !w.cu || w.cu->isComplete()) continue;
                    uint32_t pc = w.cu->getCurrentPC();
                    if (ref_pc == UINT32_MAX) ref_pc = pc;
                    else if (pc != ref_pc) { pcs_uniform = false; break; }
                }

                if (!pcs_uniform && !wg.currently_diverged) {
                    wg.currently_diverged = true;
                    ++last_divergence_events_;
                } else if (pcs_uniform && ref_pc != UINT32_MAX) {
                    wg.currently_diverged = false;  // threads reconverged
                }

                // ── Collect completed threads; retire warp when all done ──────
                bool all_done = true;
                for (auto& w : wg.threads) {
                    if (!w.active || !w.cu) continue;
                    if (w.cu->isComplete()) {
                        if (w.active) {
                            last_cycles_       += w.cu->getTotalCycles();
                            last_instructions_ += w.cu->getTotalInstructions();
                            w.active = false;
                        }
                    } else {
                        all_done = false;
                    }
                }

                if (all_done) {
                    wg.active = false;
                    if (next_warp < total_warps)
                        wg = makeWarpGroup(next_warp++);
                }
            }
            if (!any_active) break;
        }

    } else {
        // ── Single-thread-per-worker path (scalar kernels, threads_per_warp==1) ──
        const uint32_t live_units = std::max<uint32_t>(1u,
            std::min<uint32_t>(cfg_.num_compute_units == 0 ? 1u : cfg_.num_compute_units,
                               total_threads));

        std::vector<Worker> workers;
        workers.reserve(live_units);
        uint32_t next_thread = 0;
        for (; next_thread < total_threads && workers.size() < live_units; ++next_thread)
            workers.emplace_back(makeWorker(next_thread));

        uint64_t cycle = 0;
        while (true) {
            bool any_active = false;
            for (auto& worker : workers) {
                if (!worker.active || !worker.cu) continue;
                any_active = true;
                worker.cu->step();
                ++cycle;

                if (cycle % 100000 == 0)
                    std::cout << "[bridge]   ... " << cycle << " functional steps\n";

                if (worker.cu->isComplete()) {
                    last_cycles_       += worker.cu->getTotalCycles();
                    last_instructions_ += worker.cu->getTotalInstructions();
                    worker.active = false;
                    if (next_thread < total_threads)
                        worker = makeWorker(next_thread++);
                }
            }
            if (!any_active) break;
        }

        for (const auto& worker : workers)
            if (worker.simt)
                last_divergence_events_ += worker.simt->getTotalDivergenceEvents();
    }

    std::cout << "[bridge] Execution complete: "
              << last_cycles_ << " cycles, "
              << last_instructions_ << " instructions\n";

    last_l1_hits_   = static_cast<uint32_t>(mem.getL1CacheHits());
    last_l1_misses_ = static_cast<uint32_t>(mem.getL1CacheMisses());
    // last_divergence_events_ already set in the execution path above
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
