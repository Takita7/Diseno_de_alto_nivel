// fpga_driver.h - ARM↔FPGA userspace driver (T051)
//
// Maps the GPGPU AXI4-Lite register block and the FPGA global memory
// aperture into the ARM process (UIO device or /dev/mem) and provides
// device-buffer management backed by real FPGA memory.
//
// Testability: the device paths are configurable, so unit tests on x86 can
// point the driver at ordinary files that stand in for the hardware windows
// (see tests/fpga/test_fpga_driver.cpp).

#ifndef RISCV_GPGPU_FPGA_DRIVER_H
#define RISCV_GPGPU_FPGA_DRIVER_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "fpga_regs.h"

namespace riscv_gpgpu {
namespace fpga {

struct FpgaDriverConfig {
    // Register block: UIO device ("/dev/uio0") or "/dev/mem".
    std::string reg_dev_path = "/dev/uio0";
    // mmap offset into reg_dev_path. 0 for UIO; physical base for /dev/mem.
    uint64_t reg_map_offset = 0;

    // Global memory aperture: "/dev/mem" or a DMA-proxy device.
    std::string mem_dev_path = "/dev/mem";
    // mmap offset into mem_dev_path. Physical base for /dev/mem.
    uint64_t mem_map_offset = kGlobalMemPhysBase;
    // Size of the aperture to map.
    size_t mem_map_size = static_cast<size_t>(kGlobalMemSize);

    // Skip the REG_ID sanity check (used by tests against fake windows).
    bool skip_id_check = false;
};

class FpgaDriver {
public:
    FpgaDriver() = default;
    ~FpgaDriver();
    FpgaDriver(const FpgaDriver&) = delete;
    FpgaDriver& operator=(const FpgaDriver&) = delete;

    // Process-wide instance used by loader.cpp / host_api.cpp when built
    // with -DFPGA_TARGET.
    static FpgaDriver& instance();

    // ── Lifecycle ────────────────────────────────────────────────────────────
    bool open(const FpgaDriverConfig& config);
    void close();
    bool isOpen() const { return regs_ != nullptr; }

    // ── Register access (offsets from fpga_regs.h) ───────────────────────────
    uint32_t readReg(uint32_t offset) const;
    void writeReg(uint32_t offset, uint32_t value);

    // ── Control helpers ──────────────────────────────────────────────────────
    bool reset();                       // CTRL.RESET, then expect STATUS == IDLE
    void start();                       // CTRL.START
    Status status() const;              // decoded STATUS register
    // Poll STATUS until it equals `expected` or timeout_ms elapses.
    bool waitForStatus(Status expected, uint32_t timeout_ms) const;

    // ── Device memory (bump allocator over the FPGA aperture) ────────────────
    bool allocateBuffer(uint64_t& dev_addr, size_t size);
    bool freeBuffer(uint64_t dev_addr);
    bool copyToDevice(uint64_t dst_dev, const void* src_host, size_t size);
    bool copyFromDevice(void* dst_host, uint64_t src_dev, size_t size);
    size_t bufferSize(uint64_t dev_addr) const;

    // Raw aperture write/read at an absolute device address (ELF loader).
    bool writeMem(uint64_t dev_addr, const void* src_host, size_t size);
    bool readMem(void* dst_host, uint64_t dev_addr, size_t size) const;

private:
    volatile uint8_t* memAt(uint64_t dev_addr, size_t size) const;

    int reg_fd_ = -1;
    int mem_fd_ = -1;
    volatile uint32_t* regs_ = nullptr;
    volatile uint8_t* mem_ = nullptr;
    uint64_t mem_base_ = 0;   // device address of mem_[0]
    size_t mem_size_ = 0;

    uint64_t next_alloc_ = 0; // device address of next allocation
    std::map<uint64_t, size_t> buffers_;
};

} // namespace fpga
} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_FPGA_DRIVER_H
