#include "host_api.h"
#include "host_api_backend.h"

#include "../../driver/src/loader.h"
#include "../../driver/src/ptx_transpiler/ptx_transpiler.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace riscv_gpgpu {

static std::vector<uint64_t> g_staged_args;
static std::map<std::string, std::string> g_registered_ptx;
static std::set<std::string> g_required_ptx;
static std::string g_last_error;
static KernelExecutionBackend g_execution_backend = nullptr;

static bool failHostOperation(const std::string& error) {
    g_last_error = error;
    std::cerr << "[host_api] " << error << "\n";
    return false;
}

bool initializeHostAPI() {
    g_last_error.clear();
    std::cout << "[host_api] Initializing host API\n";
    return true;
}

bool shutdownHostAPI() {
    g_staged_args.clear();
    g_registered_ptx.clear();
    g_required_ptx.clear();
    g_execution_backend = nullptr;
    g_last_error.clear();
    std::cout << "[host_api] Shutting down host API\n";
    return true;
}

bool gpgpuMalloc(uint64_t& dev_ptr, size_t size) {
    return allocateDeviceBuffer(dev_ptr, size);
}

bool gpgpuFree(uint64_t dev_ptr) {
    return freeDeviceBuffer(dev_ptr);
}

bool gpgpuMemcpyH2D(uint64_t dst_dev, const void* src_host, size_t size) {
    return copyHostToDevice(dst_dev, src_host, size);
}

bool gpgpuMemcpyD2H(void* dst_host, uint64_t src_dev, size_t size) {
    return copyDeviceToHost(dst_host, src_dev, size);
}

bool setKernelArgument(const std::string& name, uint64_t value) {
    std::cout << "[host_api] arg '" << name << "' = 0x" << std::hex << value << std::dec << "\n";
    g_staged_args.push_back(value);
    return true;
}

bool clearKernelArguments() {
    g_staged_args.clear();
    return true;
}

bool gpgpuRegisterPtx(const std::string& kernel_name, const std::string& ptx_source) {
    if (kernel_name.empty()) return failHostOperation("PTX kernel name must not be empty");
    if (ptx_source.empty()) return failHostOperation("PTX source must not be empty");
    g_registered_ptx[kernel_name] = ptx_source;
    g_last_error.clear();
    return true;
}

bool gpgpuUnregisterPtx(const std::string& kernel_name) {
    if (g_registered_ptx.erase(kernel_name) == 0)
        return failHostOperation("PTX kernel is not registered: " + kernel_name);
    g_last_error.clear();
    return true;
}

bool gpgpuGetRegisteredPtx(const std::string& kernel_name, std::string& ptx_source) {
    auto it = g_registered_ptx.find(kernel_name);
    if (it == g_registered_ptx.end()) {
        ptx_source.clear();
        return false;
    }
    ptx_source = it->second;
    return true;
}

bool gpgpuRequireRegisteredPtx(const std::string& kernel_name, bool required) {
    if (kernel_name.empty()) return false;
    if (required) g_required_ptx.insert(kernel_name);
    else g_required_ptx.erase(kernel_name);
    return true;
}

bool gpgpuGetLastError(std::string& error) {
    error = g_last_error;
    return !error.empty();
}

void installKernelExecutionBackend(KernelExecutionBackend backend) {
    g_execution_backend = backend;
}

KernelExecutionBackend getKernelExecutionBackend() {
    return g_execution_backend;
}

bool gpgpuLaunchKernel(
        const std::string& kernel_name,
        uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
        uint32_t block_x, uint32_t block_y, uint32_t block_z,
        const std::vector<uint64_t>& args) {
    return gpgpuLaunchKernel(kernel_name,
                             grid_x, grid_y, grid_z,
                             block_x, block_y, block_z,
                             args, 0);
}

bool gpgpuLaunchKernel(
        const std::string& kernel_name,
        uint32_t grid_x, uint32_t grid_y, uint32_t grid_z,
        uint32_t block_x, uint32_t block_y, uint32_t block_z,
        const std::vector<uint64_t>& args,
        uint32_t shared_mem_bytes) {
    g_last_error.clear();
    if (kernel_name.empty()) return failHostOperation("Kernel name must not be empty");
    if (grid_x == 0 || grid_y == 0 || grid_z == 0
        || block_x == 0 || block_y == 0 || block_z == 0)
        return failHostOperation("Grid and block dimensions must be greater than zero");

    KernelLaunchArgs launch;
    launch.kernel_name = kernel_name;
    launch.grid_x = grid_x;
    launch.grid_y = grid_y;
    launch.grid_z = grid_z;
    launch.block_x = block_x;
    launch.block_y = block_y;
    launch.block_z = block_z;
    launch.shared_mem_bytes = shared_mem_bytes;
    launch.args = args.empty() ? g_staged_args : args;

    std::cout << "[host_api] gpgpuLaunchKernel: " << kernel_name
              << "  grid=[" << grid_x << "," << grid_y << "," << grid_z << "]"
              << "  block=[" << block_x << "," << block_y << "," << block_z << "]\n";

    auto registered = g_registered_ptx.find(kernel_name);
    if (registered == g_registered_ptx.end()) {
        if (g_required_ptx.count(kernel_name) != 0)
            return failHostOperation("Required PTX source is not registered: " + kernel_name);
        if (!configureLaunch(launch)) return failHostOperation("Failed to configure kernel launch");
        if (!startKernel()) return failHostOperation("Failed to start kernel");
        g_staged_args.clear();
        return true;
    }

    ptx::RiscvElf elf;
    try {
        ptx::PtxTranspiler transpiler;
        elf = transpiler.compile(registered->second);
    } catch (const std::exception& exception) {
        configureLaunch(launch);
        finishKernelExecution(false);
        g_staged_args.clear();
        return failHostOperation(std::string("PTX compilation exception: ") + exception.what());
    } catch (...) {
        configureLaunch(launch);
        finishKernelExecution(false);
        g_staged_args.clear();
        return failHostOperation("PTX compilation raised an unknown exception");
    }
    if (!elf.ok) {
        configureLaunch(launch);
        finishKernelExecution(false);
        g_staged_args.clear();
        return failHostOperation(elf.error.empty() ? "PTX compilation failed" : elf.error);
    }
    if (elf.bytes.empty() || elf.entry_symbol.empty()) {
        configureLaunch(launch);
        finishKernelExecution(false);
        g_staged_args.clear();
        return failHostOperation("PTX compilation produced an incomplete ELF image");
    }
    if (elf.entry_symbol != kernel_name) {
        configureLaunch(launch);
        finishKernelExecution(false);
        g_staged_args.clear();
        return failHostOperation("Registered kernel name does not match PTX entry symbol: " + elf.entry_symbol);
    }

    launch.entry_symbol = elf.entry_symbol;
    if (!configureLaunch(launch)) {
        g_staged_args.clear();
        return failHostOperation("Failed to configure transpiled kernel launch");
    }

    KernelExecutionBackend backend = getKernelExecutionBackend();
    if (backend == nullptr) {
        finishKernelExecution(false);
        g_staged_args.clear();
        return failHostOperation("No kernel execution backend is installed");
    }

    if (!beginKernelExecution()) {
        g_staged_args.clear();
        return failHostOperation("Failed to begin transpiled kernel execution");
    }

    std::string backend_error;
    bool executed = false;
    try {
        executed = backend(launch, elf.bytes, backend_error);
    } catch (const std::exception& exception) {
        backend_error = std::string("Kernel execution backend exception: ") + exception.what();
    } catch (...) {
        backend_error = "Kernel execution backend raised an unknown exception";
    }
    finishKernelExecution(executed);
    g_staged_args.clear();
    if (!executed)
        return failHostOperation(backend_error.empty() ? "Kernel execution backend failed" : backend_error);
    return true;
}

bool gpgpuSynchronize() {
    bool completed = false;
    if (!pollKernelCompletion(completed)) return false;
    std::string status;
    queryKernelStatus(status);
    std::cout << "[host_api] gpgpuSynchronize: kernel status = " << status << "\n";
    return completed;
}

} // namespace riscv_gpgpu
