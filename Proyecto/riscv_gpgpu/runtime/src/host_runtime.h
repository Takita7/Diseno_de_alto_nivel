#ifndef RISCV_GPGPU_HOST_RUNTIME_H
#define RISCV_GPGPU_HOST_RUNTIME_H

#include <cstdint>
#include <string>
#include <vector>

namespace riscv_gpgpu {

struct KernelLaunchInfo {
    std::string           name;
    std::vector<uint64_t> args;
    // Workgroup (block) dimensions.
    uint32_t workgroup_x = 1, workgroup_y = 1, workgroup_z = 1;
    // Grid dimensions (number of workgroups / blocks).
    uint32_t grid_x = 1,      grid_y = 1,      grid_z = 1;
};

// ── Thread context ────────────────────────────────────────────────────────────
//
// Describes the CUDA-style thread identity for one hardware thread.
// Injected into simulation memory at THREAD_CTX_BASE + global_id * 64.
// The kernel reads these via lw reg, N(gp):
//   tid.x   = 0(gp)    tid.y   = 4(gp)    tid.z   = 8(gp)
//   ctaid.x = 12(gp)   ctaid.y = 16(gp)   ctaid.z = 20(gp)
//   ntid.x  = 24(gp)   ntid.y  = 28(gp)   ntid.z  = 32(gp)
//
// THREAD_CTX_BASE must be above the MemoryHierarchy shared memory range
// (default: 0x0000C000) AND above the ELF code region (~0x10000..0x12000).
// KernelBridge uses 0x00200000 (2MB).
struct ThreadContext {
    uint32_t tid_x   = 0, tid_y   = 0, tid_z   = 0;
    uint32_t ctaid_x = 0, ctaid_y = 0, ctaid_z = 0;
    uint32_t ntid_x  = 1, ntid_y  = 1, ntid_z  = 1;
    uint32_t global_id = 0;  // linear thread index [0, totalThreads)
};

// Compute the ThreadContext for every thread in a kernel launch.
// Threads are numbered linearly: global_id iterates blocks outer-to-inner,
// then threads within a block outer-to-inner (z→y→x).
// The returned vector always has size grid_x*grid_y*grid_z*block_x*block_y*block_z.
std::vector<ThreadContext> computeThreadContexts(
    uint32_t grid_x,  uint32_t grid_y,  uint32_t grid_z,
    uint32_t block_x, uint32_t block_y, uint32_t block_z);

// Upload a packed kernel bundle manifest to the runtime.
bool uploadKernelBundle(const std::string& manifest_path);

// Launch the currently loaded kernel.
bool launchKernel(const KernelLaunchInfo& info);

// Poll the current kernel status string (IDLE / CONFIGURED / RUNNING / COMPLETED / FAILED).
bool pollKernelStatus(std::string& status);

// Block until the current kernel reports completion.
bool waitKernelCompletion();

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_HOST_RUNTIME_H
