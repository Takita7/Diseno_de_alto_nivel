#ifndef RISCV_GPGPU_DRIVER_LOADER_H
#define RISCV_GPGPU_DRIVER_LOADER_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace riscv_gpgpu {

// ── Kernel binary management ───────────────────────────────────────────────
bool loadKernelBinary(const std::string& path);

// ── Device memory management (host-simulated for software prototype) ───────
// Allocate a device buffer; writes a simulated device address to *dev_ptr.
bool allocateDeviceBuffer(uint64_t& dev_ptr, size_t size);
// Free a previously allocated device buffer.
bool freeDeviceBuffer(uint64_t dev_ptr);
// Copy size bytes from host src to device dst.
bool copyHostToDevice(uint64_t dst_dev, const void* src_host, size_t size);
// Copy size bytes from device src to host dst.
bool copyDeviceToHost(void* dst_host, uint64_t src_dev, size_t size);

// ── Structured kernel launch ────────────────────────────────────────────────
struct KernelLaunchArgs {
    std::string  kernel_name;
    std::string  entry_symbol;    // mangled ELF symbol, e.g. _Z10vector_addPKiS0_Pii
    uint32_t     grid_x  = 1, grid_y  = 1, grid_z  = 1;
    uint32_t     block_x = 1, block_y = 1, block_z = 1;
    std::vector<uint64_t> args;  // packed args, device ptrs as addresses
    uint32_t     shared_mem_bytes = 0;

    uint64_t gridCount() const { return static_cast<uint64_t>(grid_x) * grid_y * grid_z; }
    uint64_t blockCount() const { return static_cast<uint64_t>(block_x) * block_y * block_z; }
    uint64_t totalThreads() const { return gridCount() * blockCount(); }
};
bool configureLaunch(const KernelLaunchArgs& launch_args);
bool startKernel();

// ── Status and completion ───────────────────────────────────────────────────
bool queryKernelStatus(std::string& status);  // PENDING / RUNNING / COMPLETED / FAILED
bool pollKernelCompletion(bool& completed);

// ── Legacy shim (kept for backward compatibility) ──────────────────────────
bool configureKernel(const std::string& kernel_name, const std::vector<uint64_t>& args);

// ─── Device buffer inspection (used by KernelBridge) ─────────────────────────
// Read the current contents of a simulated device buffer.
bool getDeviceBufferContent(uint64_t dev_ptr, std::vector<uint8_t>& data);
// Overwrite the contents of a simulated device buffer (used to copy results back).
bool setDeviceBufferContent(uint64_t dev_ptr, const std::vector<uint8_t>& data);
// Return the size of an allocated device buffer (0 if not found).
size_t getDeviceBufferSize(uint64_t dev_ptr);
// Return the currently configured launch arguments.
bool getCurrentLaunchArgs(KernelLaunchArgs& out_args);
// Return the loaded kernel binary path (set by loadKernelBinary).
const std::string& getKernelBinaryPath();

} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_DRIVER_LOADER_H
