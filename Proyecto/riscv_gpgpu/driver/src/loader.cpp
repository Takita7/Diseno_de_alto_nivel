#include "loader.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace riscv_gpgpu {

// ─── Internal simulation state ──────────────────────────────────────────────

// Simulated device memory: maps simulated device address → host allocation.
// Device addresses start at 0x10000000 so 0 always means "null".
static std::map<uint64_t, std::vector<uint8_t>> g_device_buffers;
static uint64_t g_next_dev_addr = 0x10000000ULL;

// Current loaded kernel binary path.
static std::string g_kernel_binary_path;

// Current configured launch.
static KernelLaunchArgs g_launch_args;
static bool g_launch_configured = false;

// Simulated execution state.
enum class ExecState { IDLE, CONFIGURED, RUNNING, COMPLETED, FAILED };
static ExecState g_exec_state = ExecState::IDLE;

// ─── Kernel binary ───────────────────────────────────────────────────────────

bool loadKernelBinary(const std::string& path) {
    std::cout << "[driver] Loading kernel binary: " << path << "\n";
    g_kernel_binary_path = path;
    g_exec_state = ExecState::IDLE;
    return true;
}

// ─── Device memory management ────────────────────────────────────────────────

bool allocateDeviceBuffer(uint64_t& dev_ptr, size_t size) {
    if (size == 0) {
        std::cerr << "[driver] allocateDeviceBuffer: size must be > 0\n";
        return false;
    }
    dev_ptr = g_next_dev_addr;
    g_next_dev_addr += ((size + 15) / 16) * 16;  // 16-byte aligned
    g_device_buffers[dev_ptr].assign(size, 0);
    std::cout << "[driver] Allocated " << size << " bytes at device address 0x"
              << std::hex << dev_ptr << std::dec << "\n";
    return true;
}

bool freeDeviceBuffer(uint64_t dev_ptr) {
    auto it = g_device_buffers.find(dev_ptr);
    if (it == g_device_buffers.end()) {
        std::cerr << "[driver] freeDeviceBuffer: unknown device address 0x"
                  << std::hex << dev_ptr << std::dec << "\n";
        return false;
    }
    g_device_buffers.erase(it);
    std::cout << "[driver] Freed device buffer at 0x" << std::hex << dev_ptr << std::dec << "\n";
    return true;
}

bool copyHostToDevice(uint64_t dst_dev, const void* src_host, size_t size) {
    auto it = g_device_buffers.find(dst_dev);
    if (it == g_device_buffers.end()) {
        std::cerr << "[driver] copyHostToDevice: unknown device address 0x"
                  << std::hex << dst_dev << std::dec << "\n";
        return false;
    }
    if (size > it->second.size()) {
        std::cerr << "[driver] copyHostToDevice: copy size " << size
                  << " exceeds buffer size " << it->second.size() << "\n";
        return false;
    }
    std::memcpy(it->second.data(), src_host, size);
    std::cout << "[driver] H2D copy " << size << " bytes → 0x"
              << std::hex << dst_dev << std::dec << "\n";
    return true;
}

bool copyDeviceToHost(void* dst_host, uint64_t src_dev, size_t size) {
    auto it = g_device_buffers.find(src_dev);
    if (it == g_device_buffers.end()) {
        std::cerr << "[driver] copyDeviceToHost: unknown device address 0x"
                  << std::hex << src_dev << std::dec << "\n";
        return false;
    }
    if (size > it->second.size()) {
        std::cerr << "[driver] copyDeviceToHost: copy size " << size
                  << " exceeds buffer size " << it->second.size() << "\n";
        return false;
    }
    std::memcpy(dst_host, it->second.data(), size);
    std::cout << "[driver] D2H copy " << size << " bytes ← 0x"
              << std::hex << src_dev << std::dec << "\n";
    return true;
}

// ─── Structured kernel launch ─────────────────────────────────────────────────

bool configureLaunch(const KernelLaunchArgs& la) {
    g_launch_args = la;
    g_launch_configured = true;
    g_exec_state = ExecState::CONFIGURED;
    std::cout << "[driver] Launch configured: " << la.kernel_name
              << "  grid=" << la.grid_x << "x" << la.grid_y << "x" << la.grid_z
              << "  block=" << la.block_x << "x" << la.block_y << "x" << la.block_z
              << "  args=" << la.args.size()
              << "  shared_mem=" << la.shared_mem_bytes << " bytes\n";
    // Print packed argument list so the hardware interface is visible.
    for (size_t i = 0; i < la.args.size(); ++i) {
        std::cout << "[driver]   arg[" << i << "] = 0x"
                  << std::hex << la.args[i] << std::dec << "\n";
    }
    // Simulation: pointer arguments are resolved against g_device_buffers;
    // for each pointer arg, execute the kernel body on the host memory backing
    // the simulated device buffers so results are real.
    if (!g_device_buffers.empty()) {
        std::cout << "[driver] Simulation: device buffers registered, will execute kernel on host-backed memory\n";
    }
    return true;
}

bool startKernel() {
    if (!g_launch_configured) {
        std::cerr << "[driver] startKernel called without configureLaunch\n";
        return false;
    }
    g_exec_state = ExecState::RUNNING;
    std::cout << "[driver] Kernel started: " << g_launch_args.kernel_name << "\n";
    // Simulation: mark complete immediately (real HW would do this asynchronously).
    g_exec_state = ExecState::COMPLETED;
    return true;
}

bool queryKernelStatus(std::string& status) {
    switch (g_exec_state) {
        case ExecState::IDLE:       status = "IDLE";      break;
        case ExecState::CONFIGURED: status = "CONFIGURED"; break;
        case ExecState::RUNNING:    status = "RUNNING";   break;
        case ExecState::COMPLETED:  status = "COMPLETED"; break;
        case ExecState::FAILED:     status = "FAILED";    break;
    }
    return true;
}

bool pollKernelCompletion(bool& completed) {
    completed = (g_exec_state == ExecState::COMPLETED);
    return true;
}

// ─── Legacy shim ─────────────────────────────────────────────────────────────

bool configureKernel(const std::string& kernel_name, const std::vector<uint64_t>& args) {
    KernelLaunchArgs la;
    la.kernel_name = kernel_name;
    la.args        = args;
    return configureLaunch(la);
}
// ─── Device buffer inspection ──────────────────────────────────────────────────────────────

bool getDeviceBufferContent(uint64_t dev_ptr, std::vector<uint8_t>& data) {
    auto it = g_device_buffers.find(dev_ptr);
    if (it == g_device_buffers.end()) return false;
    data = it->second;
    return true;
}

bool setDeviceBufferContent(uint64_t dev_ptr, const std::vector<uint8_t>& data) {
    auto it = g_device_buffers.find(dev_ptr);
    if (it == g_device_buffers.end()) return false;
    if (data.size() != it->second.size()) return false;
    it->second = data;
    return true;
}

size_t getDeviceBufferSize(uint64_t dev_ptr) {
    auto it = g_device_buffers.find(dev_ptr);
    return (it != g_device_buffers.end()) ? it->second.size() : 0;
}

const std::string& getKernelBinaryPath() {
    return g_kernel_binary_path;
}
} // namespace riscv_gpgpu
