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
#include "../top/top.h"
#include "../memory/memory_hierarchy.h"
#include "../compute_unit/compute_unit.h"

// Driver API
#include "../../../driver/src/loader.h"

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <systemc>

namespace riscv_gpgpu {

KernelBridge::KernelBridge(Config cfg) : cfg_(cfg) {}

bool KernelBridge::runOnHardware(const std::string& kernel_name,
                                  const std::string& binary_path,
                                  const std::vector<uint64_t>& kernel_args,
                                  const std::vector<uint64_t>& device_ptrs) {

    std::cout << "[bridge] Starting hardware simulation for kernel '" << kernel_name << "'\n";
    std::cout << "[bridge] ELF binary: " << binary_path << "\n";

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
    if (elf.findSymbol(kernel_name, sym)) {
        entry_pc = sym.address;
        std::cout << "[bridge] Entry point '" << sym.name
                  << "' at 0x" << std::hex << entry_pc << std::dec << "\n";
    } else {
        entry_pc = elf.getEntryPoint();
        std::cout << "[bridge] Symbol '" << kernel_name
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

    // ── 6. Create and run ComputeUnit ─────────────────────────────────────────
    ComputeUnit::Config cu_cfg;
    cu_cfg.unit_id          = 0;
    cu_cfg.num_threads      = cfg_.threads_per_warp;
    cu_cfg.threads_per_warp = cfg_.threads_per_warp;
    cu_cfg.max_warps        = 1;  // one warp for functional sim
    cu_cfg.shared_mem_size  = cfg_.shared_mem_size;
    cu_cfg.max_cycles       = cfg_.max_sim_cycles;

    ComputeUnit cu("bridge_cu", cu_cfg);
    cu.setMemoryHierarchy(&mem);
    cu.setEntryPoint(entry_pc);
    cu.setInitialRegisters(regs);
    cu.setReturnSentinel(RETURN_SENTINEL);
    cu.launchKernel(0, 1, 1);

    // Functional loop — no sc_start, just direct step() calls
    uint64_t cycle = 0;
    while (!cu.isComplete()) {
        cu.step();
        ++cycle;
        if (cycle % 100000 == 0) {
            std::cout << "[bridge]   ... " << cycle << " cycles\n";
        }
    }

    std::cout << "[bridge] Execution complete: "
              << cu.getTotalCycles() << " cycles, "
              << cu.getTotalInstructions() << " instructions\n";

    last_cycles_       = cu.getTotalCycles();
    last_instructions_ = cu.getTotalInstructions();
    last_l1_hits_      = static_cast<uint32_t>(mem.getL1CacheHits());
    last_l1_misses_    = static_cast<uint32_t>(mem.getL1CacheMisses());

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
