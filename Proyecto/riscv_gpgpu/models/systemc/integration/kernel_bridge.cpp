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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <systemc>

namespace riscv_gpgpu {

KernelBridge::KernelBridge(Config cfg) : cfg_(cfg) {}

bool KernelBridge::runOnHardware(const std::string& kernel_name,
                                  const std::string& binary_path,
                                  const std::vector<uint64_t>& kernel_args,
                                  const std::vector<uint64_t>& device_ptrs) {
    const bool bridge_debug = []() {
        const char* value = std::getenv("KERNEL_BRIDGE_DEBUG");
        return value && std::string(value) == "1";
    }();

    last_error_.clear();
    auto fail = [this](const std::string& error) {
        last_error_ = error;
        std::cerr << "[bridge] " << error << "\n";
        return false;
    };

    static std::atomic<uint64_t> run_sequence{0};
    const uint64_t run_id = run_sequence.fetch_add(1, std::memory_order_relaxed);
    const std::string run_prefix = "bridge_run_" + std::to_string(run_id);

    auto parseEnvU64 = [](const char* name, uint64_t fallback) {
        const char* value = std::getenv(name);
        if (!value) return fallback;
        char* end = nullptr;
        unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end != value && *end == '\0') {
            return static_cast<uint64_t>(parsed);
        }
        return fallback;
    };

    const uint64_t trace_interval = bridge_debug
        ? parseEnvU64("KERNEL_BRIDGE_TRACE_INTERVAL", 250000)
        : 0;

    std::cout << "[bridge] Starting hardware simulation for kernel '" << kernel_name << "'\n";
    std::cout << "[bridge] ELF binary: " << binary_path << "\n";

    KernelLaunchArgs launch_args;
    const bool have_launch_args = getCurrentLaunchArgs(launch_args);
    const bool use_launch_args = have_launch_args && (launch_args.kernel_name == kernel_name);
    if (have_launch_args) {
        std::cout << "[bridge] Driver launch available: "
                  << launch_args.kernel_name
                  << " entry=" << (launch_args.entry_symbol.empty() ? "<unresolved>" : launch_args.entry_symbol)
                  << " grid=" << launch_args.grid_x << "x" << launch_args.grid_y << "x" << launch_args.grid_z
                  << " block=" << launch_args.block_x << "x" << launch_args.block_y << "x" << launch_args.block_z
                  << " shared_mem=" << launch_args.shared_mem_bytes << "\n";
        if (!use_launch_args) {
            std::cout << "[bridge] Driver launch metadata does not match kernel '"
                      << kernel_name << "'; using direct kernel arguments\n";
        }
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
    if (!elf.load(binary_path, mem))
        return fail("Failed to load ELF: " + binary_path);

    SymbolEntry cuda_thread_ctx_sym;
    const bool has_cuda_thread_ctx = elf.findSymbol("__gpgpu_thread_context", cuda_thread_ctx_sym);
    if (has_cuda_thread_ctx) {
        std::cout << "[bridge] CUDA compat thread context symbol at 0x"
                  << std::hex << cuda_thread_ctx_sym.address << std::dec << "\n";
    }

    struct BufferWatch {
        std::string label;
        uint32_t addr = 0;
        uint32_t len = 0;
    };
    std::vector<BufferWatch> watches;
    if (bridge_debug) {
        if (kernel_name == "Kernel" && kernel_args.size() >= 7) {
            watches.push_back(BufferWatch{"mask",     static_cast<uint32_t>(kernel_args[2]), 5});
            watches.push_back(BufferWatch{"updating", static_cast<uint32_t>(kernel_args[3]), 5});
            watches.push_back(BufferWatch{"visited",  static_cast<uint32_t>(kernel_args[4]), 5});
            watches.push_back(BufferWatch{"cost",     static_cast<uint32_t>(kernel_args[5]), 20});
        } else if (kernel_name == "Kernel2" && kernel_args.size() >= 4) {
            watches.push_back(BufferWatch{"mask",     static_cast<uint32_t>(kernel_args[0]), 5});
            watches.push_back(BufferWatch{"updating", static_cast<uint32_t>(kernel_args[1]), 5});
            watches.push_back(BufferWatch{"visited",  static_cast<uint32_t>(kernel_args[2]), 5});
            watches.push_back(BufferWatch{"over",     static_cast<uint32_t>(kernel_args[3]), 1});
        }
    }

    // ── 3. Copy device buffers into SystemC global memory ────────────────────
    for (uint64_t dev_ptr : device_ptrs) {
        std::vector<uint8_t> content;
        if (!getDeviceBufferContent(dev_ptr, content))
            return fail("Device buffer not found: " + std::to_string(dev_ptr));
        mem.writeBytes(dev_ptr, content.data(), content.size());
        std::cout << "[bridge] Mapped device buffer 0x" << std::hex << dev_ptr
                  << std::dec << " (" << content.size() << " bytes) into sim memory\n";
    }

    // ── 4. Resolve entry point ────────────────────────────────────────────────
    uint32_t entry_pc = 0;
    SymbolEntry sym;
    const std::string& entry_name = (use_launch_args && !launch_args.entry_symbol.empty())
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
    if (kernel_args.size() > 8)
        return fail("RV32 ABI supports at most 8 kernel arguments");
    std::array<uint32_t, 32> regs{};
    for (size_t i = 0; i < kernel_args.size(); ++i) {
        if ((kernel_args[i] >> 32) != 0)
            return fail("Kernel argument " + std::to_string(i) + " does not fit RV32");
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
    // Each worker needs its own stack region because the bridge interleaves
    // instructions from multiple workers in a single shared memory space.
    const uint32_t STACK_BASE        = 0x30000000u;
    const uint32_t STACK_STRIDE      = 0x00010000u;  // 64 KiB per worker

    std::cout << "[bridge] Initial registers: "
              << "sp=0x" << std::hex << regs[2]
              << " ra=0x" << regs[1] << std::dec;
    if (!kernel_args.empty())
        std::cout << " a0..a" << (kernel_args.size() - 1) << "=[args]";
    std::cout << "\n";

    // ── 6. Create and run functional compute units ────────────────────────────
    const uint32_t effective_grid_x = use_launch_args ? launch_args.grid_x : 1;
    const uint32_t effective_grid_y = use_launch_args ? launch_args.grid_y : 1;
    const uint32_t effective_grid_z = use_launch_args ? launch_args.grid_z : 1;
    const uint32_t effective_block_x = use_launch_args ? launch_args.block_x : 1;
    const uint32_t effective_block_y = use_launch_args ? launch_args.block_y : 1;
    const uint32_t effective_block_z = use_launch_args ? launch_args.block_z : 1;
    const uint32_t total_blocks = std::max<uint32_t>(1u,
        effective_grid_x * std::max<uint32_t>(1u, effective_grid_y) * std::max<uint32_t>(1u, effective_grid_z));
    // Threads per block: for single-thread kernels this is 1; for SIMT kernels
    // this is block_x * block_y * block_z (each gets its own a4/a5 values).
    const uint32_t threads_per_block = std::max<uint32_t>(1u,
        effective_block_x * std::max<uint32_t>(1u, effective_block_y) * std::max<uint32_t>(1u, effective_block_z));
    const uint32_t total_threads = total_blocks * threads_per_block;
    if (bridge_debug) {
        std::cout << "[bridge-debug] requested_cus=" << cfg_.num_compute_units
                  << " total_blocks=" << total_blocks
                  << " threads_per_block=" << threads_per_block
                  << " total_threads=" << total_threads
                  << " threads_per_warp=" << cfg_.threads_per_warp
                  << "\n";
    }
    const uint32_t shared_mem_bytes = use_launch_args
        ? launch_args.shared_mem_bytes
        : cfg_.shared_mem_size;
    if (shared_mem_bytes > cfg_.shared_mem_size)
        return fail("Requested shared memory exceeds configured capacity");
    for (uint32_t block_id = 0; block_id < total_blocks; ++block_id)
        if (!mem.allocateSharedMemory(block_id, shared_mem_bytes))
            return fail("Failed to allocate shared memory for block " + std::to_string(block_id));

    struct Worker {
        std::unique_ptr<ComputeUnit>    cu;
        std::unique_ptr<SIMTController> simt;
        uint32_t block_id  = 0;
        uint32_t thread_id = 0;
        bool     active    = false;
        uint64_t stalled_pc_steps = 0;
        uint64_t blocked_steps = 0;
        uint32_t last_pc = 0;
        std::array<uint32_t, 12> cuda_thread_ctx{};
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
        // x2 (sp) = private per-thread stack top to avoid stack-frame aliasing.
        worker_regs[2] = STACK_BASE + global_thread_id * STACK_STRIDE + (STACK_STRIDE - 16u);
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
        const std::array<uint32_t, 9> thread_ctx = {
            tid_x, tid_y, tid_z,
            ctaid_x, ctaid_y, ctaid_z,
            std::max<uint32_t>(1u, effective_block_x),
            std::max<uint32_t>(1u, effective_block_y),
            std::max<uint32_t>(1u, effective_block_z)
        };
        mem.writeBytes(ctx_base, reinterpret_cast<const uint8_t*>(thread_ctx.data()), thread_ctx.size() * sizeof(uint32_t));

        worker.cuda_thread_ctx = {
            tid_x, tid_y, tid_z,
            ctaid_x, ctaid_y, ctaid_z,
            std::max<uint32_t>(1u, effective_block_x),
            std::max<uint32_t>(1u, effective_block_y),
            std::max<uint32_t>(1u, effective_block_z),
            std::max<uint32_t>(1u, effective_grid_x),
            std::max<uint32_t>(1u, effective_grid_y),
            std::max<uint32_t>(1u, effective_grid_z)
        };
        worker.cu->setReturnSentinel(RETURN_SENTINEL);
        worker.cu->setBlockID(block_id);
        worker.cu->launchKernel(block_id, effective_grid_x, effective_grid_y);
        worker.last_pc = entry_pc;

        worker.block_id  = block_id;
        worker.thread_id = thread_id;
        worker.active    = true;
        return worker;
    };

    last_cycles_            = 0;
    last_instructions_      = 0;
    last_divergence_events_ = 0;
    last_worker_count_      = total_threads;
    last_active_units_      = 1;
    last_worker_cycles_total_ = 0;
    last_worker_cycles_mean_  = 0.0;
    last_worker_cycles_max_    = 0;
    last_effective_cycles_     = 0;
    last_parallelism_factor_   = 0.0;

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

        const uint32_t warps_per_block = (threads_per_block + warp_size - 1) / warp_size;
        const uint32_t total_warps = total_blocks * warps_per_block;
        last_active_units_ = warp_size;

        auto makeWarpGroup = [&](uint32_t warp_idx) {
            WarpGroup wg;
            const uint32_t block_id = warp_idx / warps_per_block;
            const uint32_t local_base = (warp_idx % warps_per_block) * warp_size;
            for (uint32_t t = 0; t < warp_size && local_base + t < threads_per_block; ++t)
                wg.threads.emplace_back(makeWorker(block_id * threads_per_block + local_base + t));
            return wg;
        };

        std::vector<WarpGroup> warp_groups;
        warp_groups.reserve(total_warps);
        for (uint32_t warp_idx = 0; warp_idx < total_warps; ++warp_idx)
            warp_groups.emplace_back(makeWarpGroup(warp_idx));

        uint64_t cycle = 0;
        while (true) {
            bool any_active = false;
            bool made_progress = false;
            for (auto& wg : warp_groups) {
                if (!wg.active) continue;
                any_active = true;

                // Step all non-complete threads in this warp simultaneously.
                for (auto& w : wg.threads)
                    if (w.active && w.cu && !w.cu->isComplete() && !w.cu->isBlocked()) {
                        made_progress = true;
                        if (has_cuda_thread_ctx) {
                            mem.writeBytes(cuda_thread_ctx_sym.address,
                                reinterpret_cast<const uint8_t*>(w.cuda_thread_ctx.data()),
                                w.cuda_thread_ctx.size() * sizeof(uint32_t));
                        }
                        w.cu->step();
                    }
                ++cycle;

                for (auto& w : wg.threads) {
                    if (!w.active || !w.cu || !w.cu->isBlocked()) continue;
                    const uint32_t barrier_id = w.cu->getBlockedBarrierID();
                    uint32_t arrived = 0;
                    for (auto& other_wg : warp_groups)
                        for (auto& other : other_wg.threads)
                            if (other.active && other.block_id == w.block_id && other.cu
                                && other.cu->isBlocked()
                                && other.cu->getBlockedBarrierID() == barrier_id)
                                ++arrived;
                    if (arrived == threads_per_block) {
                        for (auto& other_wg : warp_groups)
                            for (auto& other : other_wg.threads)
                                if (other.active && other.block_id == w.block_id && other.cu)
                                    other.cu->releaseBarrier(barrier_id);
                        made_progress = true;
                    }
                }

                if (cycle % 100000 == 0)
                    std::cout << "[bridge]   ... " << cycle << " functional warp-steps\n";

                // ── SIMT divergence detection: compare PCs across warp ────────
                // A transition from uniform to non-uniform PC = one divergence event.
                uint32_t ref_pc      = UINT32_MAX;
                bool     pcs_uniform = true;
                for (auto& w : wg.threads) {
                    if (!w.active || !w.cu || w.cu->isComplete() || w.cu->isBlocked()) continue;
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
                            const uint64_t worker_cycles = w.cu->getTotalCycles();
                            last_cycles_       += w.cu->getTotalCycles();
                            last_instructions_ += w.cu->getTotalInstructions();
                            last_worker_cycles_total_ += worker_cycles;
                            last_worker_cycles_max_ = std::max(last_worker_cycles_max_, worker_cycles);
                            w.active = false;
                        }
                    } else {
                        all_done = false;
                    }
                }

                if (all_done) wg.active = false;
            }
            if (!any_active) break;
            if (!made_progress)
                return fail("Barrier deadlock in warp scheduler");
        }

    } else {
        // ── Single-thread-per-worker path (scalar kernels, threads_per_warp==1) ──
        std::vector<Worker> workers;
        workers.reserve(total_threads);
        for (uint32_t thread = 0; thread < total_threads; ++thread)
            workers.emplace_back(makeWorker(thread));

        const uint32_t active_units = std::max<uint32_t>(1u,
            std::min(cfg_.num_compute_units, static_cast<uint32_t>(workers.size())));
        last_active_units_ = active_units;
        if (bridge_debug) {
            std::cout << "[bridge-debug] scalar scheduler active_units=" << active_units
                      << " of total_workers=" << workers.size() << "\n";
        }

        uint64_t cycle = 0;
        size_t worker_cursor = 0;
        while (true) {
            bool any_active = false;
            bool made_progress = false;
            const size_t total_workers = workers.size();
            size_t selected = 0;
            for (size_t scanned = 0; scanned < total_workers && selected < active_units; ++scanned) {
                const size_t worker_index = (worker_cursor + scanned) % total_workers;
                auto& worker = workers[worker_index];
                if (!worker.active || !worker.cu) continue;
                any_active = true;
                if (worker.cu->isBlocked()) {
                    ++worker.blocked_steps;
                    continue;
                }

                ++selected;
                made_progress = true;
                const uint32_t pc_before = worker.cu->getCurrentPC();
                if (has_cuda_thread_ctx) {
                    mem.writeBytes(cuda_thread_ctx_sym.address,
                        reinterpret_cast<const uint8_t*>(worker.cuda_thread_ctx.data()),
                        worker.cuda_thread_ctx.size() * sizeof(uint32_t));
                }
                worker.cu->step();
                ++cycle;
                const uint32_t pc_after = worker.cu->getCurrentPC();
                if (!worker.cu->isComplete()) {
                    if (pc_after == pc_before) ++worker.stalled_pc_steps;
                    else worker.stalled_pc_steps = 0;
                }
                worker.last_pc = pc_after;

                if (worker.cu->isBlocked()) {
                    const uint32_t barrier_id = worker.cu->getBlockedBarrierID();
                    uint32_t arrived = 0;
                    for (const auto& other : workers)
                        if (other.active && other.block_id == worker.block_id && other.cu
                            && other.cu->isBlocked()
                            && other.cu->getBlockedBarrierID() == barrier_id)
                            ++arrived;
                    if (arrived == threads_per_block)
                        for (auto& other : workers)
                            if (other.active && other.block_id == worker.block_id && other.cu)
                                other.cu->releaseBarrier(barrier_id);
                }

                if (cycle % 100000 == 0)
                    std::cout << "[bridge]   ... " << cycle << " functional steps\n";

                if (bridge_debug && trace_interval > 0 && (cycle % trace_interval == 0)) {
                    std::cout << "[bridge-debug] cycle=" << cycle << " worker-snapshot\n";
                    for (const auto& w : workers) {
                        if (!w.cu) continue;
                        std::cout << "[bridge-debug]   tid=" << w.thread_id
                                  << " block=" << w.block_id
                                  << " active=" << w.active
                                  << " blocked=" << w.cu->isBlocked()
                                  << " complete=" << w.cu->isComplete()
                                  << " pc=0x" << std::hex << w.last_pc << std::dec
                                  << " total_cycles=" << w.cu->getTotalCycles()
                                  << " stalled_pc_steps=" << w.stalled_pc_steps
                                  << " blocked_steps=" << w.blocked_steps
                                  << "\n";
                    }

                    for (const auto& watch : watches) {
                        std::vector<uint8_t> bytes(watch.len, 0);
                        mem.readBytes(watch.addr, bytes.data(), bytes.size());
                        std::ostringstream oss;
                        oss << "[bridge-debug]   watch " << watch.label
                            << " @0x" << std::hex << watch.addr << std::dec << " =";
                        if (watch.label == "cost" && watch.len % 4 == 0) {
                            for (size_t i = 0; i < watch.len / 4; ++i) {
                                int32_t value = static_cast<int32_t>(
                                    static_cast<uint32_t>(bytes[i * 4])
                                    | (static_cast<uint32_t>(bytes[i * 4 + 1]) << 8)
                                    | (static_cast<uint32_t>(bytes[i * 4 + 2]) << 16)
                                    | (static_cast<uint32_t>(bytes[i * 4 + 3]) << 24));
                                oss << " " << value;
                            }
                        } else {
                            for (uint8_t b : bytes) {
                                oss << " " << static_cast<uint32_t>(b);
                            }
                        }
                        std::cout << oss.str() << "\n";
                    }
                }

                if (worker.cu->isComplete()) {
                    const uint64_t worker_cycles = worker.cu->getTotalCycles();
                    last_cycles_       += worker.cu->getTotalCycles();
                    last_instructions_ += worker.cu->getTotalInstructions();
                    last_worker_cycles_total_ += worker_cycles;
                    last_worker_cycles_max_ = std::max(last_worker_cycles_max_, worker_cycles);
                    worker.active = false;
                }
            }
            if (total_workers > 0)
                worker_cursor = (worker_cursor + active_units) % total_workers;
            if (!any_active) break;
            if (!made_progress)
                return fail("Barrier deadlock in scalar scheduler");
        }

        for (const auto& worker : workers)
            if (worker.simt)
                last_divergence_events_ += worker.simt->getTotalDivergenceEvents();
    }

    if (last_worker_count_ > 0)
        last_worker_cycles_mean_ = static_cast<double>(last_worker_cycles_total_)
                                 / static_cast<double>(last_worker_count_);
    if (last_active_units_ == 0)
        last_active_units_ = 1;
    if (last_active_units_ > 0) {
        last_effective_cycles_ = (last_worker_cycles_total_ + last_active_units_ - 1)
                               / last_active_units_;
    }
    if (last_worker_cycles_max_ > 0)
        last_parallelism_factor_ = static_cast<double>(last_worker_cycles_total_)
                                 / static_cast<double>(last_worker_cycles_max_);

    std::cout << "[bridge] Execution complete: "
              << last_cycles_ << " cycles, "
              << last_instructions_ << " instructions\n";

    std::cout << "[bridge] Workload metrics: scheduler="
              << (warp_size > 1 ? "warp" : "scalar")
              << " workers=" << last_worker_count_
              << " active_units=" << last_active_units_
              << " worker_cycles_total=" << last_worker_cycles_total_
              << " worker_cycles_mean=" << last_worker_cycles_mean_
              << " worker_cycles_max=" << last_worker_cycles_max_
              << " effective_cycles=" << last_effective_cycles_
              << " parallelism_factor=" << last_parallelism_factor_
              << "\n";

    last_l1_hits_   = static_cast<uint32_t>(mem.getL1CacheHits());
    last_l1_misses_ = static_cast<uint32_t>(mem.getL1CacheMisses());
    // last_divergence_events_ already set in the execution path above
    for (uint32_t block_id = 0; block_id < total_blocks; ++block_id)
        mem.releaseSharedMemory(block_id);

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
        if (!setDeviceBufferContent(dev_ptr, result))
            return fail("Failed to write results to device buffer " + std::to_string(dev_ptr));
        else {
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
