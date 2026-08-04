#include "host_runtime.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include "../../software/kernel_loader/kernel_loader.h"
#include "../../driver/src/loader.h"

namespace riscv_gpgpu {

// ── Thread context mapping ────────────────────────────────────────────────────
//
// Linear thread numbering matches KernelBridge::makeWorker():
//   global_thread_id = block_idx * block_size + thread_idx_within_block
// Blocks iterate z→y→x (x fastest); threads within block iterate z→y→x (x fastest).

std::vector<ThreadContext> computeThreadContexts(
    uint32_t grid_x,  uint32_t grid_y,  uint32_t grid_z,
    uint32_t block_x, uint32_t block_y, uint32_t block_z)
{
    grid_x  = std::max(1u, grid_x);
    grid_y  = std::max(1u, grid_y);
    grid_z  = std::max(1u, grid_z);
    block_x = std::max(1u, block_x);
    block_y = std::max(1u, block_y);
    block_z = std::max(1u, block_z);

    const uint32_t total_blocks  = grid_x  * grid_y  * grid_z;
    const uint32_t block_size    = block_x * block_y * block_z;
    const uint32_t total_threads = total_blocks * block_size;

    std::vector<ThreadContext> ctxs;
    ctxs.reserve(total_threads);

    for (uint32_t t = 0; t < total_threads; ++t) {
        const uint32_t blk_id = t / block_size;
        const uint32_t thd_id = t % block_size;

        ThreadContext ctx;
        ctx.global_id = t;
        ctx.ntid_x    = block_x;
        ctx.ntid_y    = block_y;
        ctx.ntid_z    = block_z;
        ctx.tid_x     = thd_id % block_x;
        ctx.tid_y     = (thd_id / block_x) % block_y;
        ctx.tid_z     = thd_id / (block_x * block_y);
        ctx.ctaid_x   = blk_id % grid_x;
        ctx.ctaid_y   = (blk_id / grid_x) % grid_y;
        ctx.ctaid_z   = blk_id / (grid_x * grid_y);
        ctxs.push_back(ctx);
    }
    return ctxs;
}

static std::string current_kernel_name;
static std::string current_binary_path;
static std::string current_entry_symbol;
static uint64_t    current_binary_size  = 0;
static bool        current_kernel_loaded = false;
static uint32_t    current_workgroup_x = 1;
static uint32_t    current_workgroup_y = 1;
static uint32_t    current_workgroup_z = 1;
static uint64_t    current_shared_mem_bytes = 0;

// ─── Bundle upload ─────────────────────────────────────────────────────────
bool uploadKernelBundle(const std::string& manifest_path) {
    KernelBundleInfo info;
    if (!inspectKernelBundleDetails(manifest_path, info))
        return false;
    if (!loadKernelBinary(info.binary_path))
        return false;
    current_kernel_name   = info.kernel_name;
    current_binary_path   = info.binary_path;
    current_entry_symbol  = info.entry_symbol;
    current_binary_size   = info.binary_size;
    current_kernel_loaded = true;
    current_workgroup_x   = info.workgroup_x;
    current_workgroup_y   = info.workgroup_y;
    current_workgroup_z   = info.workgroup_z;
    current_shared_mem_bytes = info.shared_mem_bytes;
    std::cout << "[runtime] Kernel entry symbol: "
              << (current_entry_symbol.empty() ? "<none>" : current_entry_symbol) << "\n";
    std::cout << "[runtime] Kernel bundle loaded: " << current_kernel_name
              << " (" << info.binary_size << " bytes)"
              << "  workgroup=[" << current_workgroup_x << "," << current_workgroup_y << "," << current_workgroup_z << "]"
              << "  shared_mem=" << current_shared_mem_bytes << " bytes\n";
    return true;
}

// ─── Kernel launch ──────────────────────────────────────────────────────────
bool launchKernel(const KernelLaunchInfo& info) {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel loaded\n";
        return false;
    }
    if (info.name != current_kernel_name) {
        std::cerr << "[runtime] Kernel name mismatch: expected '" << current_kernel_name
                  << "' got '" << info.name << "'\n";
        return false;
    }
    KernelLaunchArgs la;
    la.kernel_name = info.name;
    la.entry_symbol = current_entry_symbol;
    la.grid_x  = info.grid_x;      la.grid_y  = info.grid_y;      la.grid_z  = info.grid_z;
    la.block_x = (info.workgroup_x == 1 && current_workgroup_x != 1) ? current_workgroup_x : info.workgroup_x;
    la.block_y = (info.workgroup_y == 1 && current_workgroup_y != 1) ? current_workgroup_y : info.workgroup_y;
    la.block_z = (info.workgroup_z == 1 && current_workgroup_z != 1) ? current_workgroup_z : info.workgroup_z;
    la.shared_mem_bytes = current_shared_mem_bytes;
    la.args    = info.args;
    if (!configureLaunch(la))  return false;
    if (!startKernel())        return false;
    std::cout << "[runtime] Launched '" << info.name << "'"
              << "  grid=[" << info.grid_x << "," << info.grid_y << "," << info.grid_z << "]"
                 << "  block=[" << la.block_x << "," << la.block_y << "," << la.block_z << "]"
                 << "  entry=" << (la.entry_symbol.empty() ? "<unresolved>" : la.entry_symbol) << "\n";
    return true;
}

// ─── Status ───────────────────────────────────────────────────────────────────
bool pollKernelStatus(std::string& status) {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel loaded\n";
        return false;
    }
    if (!queryKernelStatus(status)) return false;
    std::cout << "[runtime] Kernel '" << current_kernel_name << "' status: " << status << "\n";
    return true;
}

bool waitKernelCompletion() {
    if (!current_kernel_loaded) {
        std::cerr << "[runtime] No kernel loaded\n";
        return false;
    }
    bool completed = false;
    if (!pollKernelCompletion(completed)) return false;
    std::string status;
    queryKernelStatus(status);
    std::cout << "[runtime] Kernel '" << current_kernel_name << "' completed with status: " << status << "\n";
    return completed;
}

} // namespace riscv_gpgpu
