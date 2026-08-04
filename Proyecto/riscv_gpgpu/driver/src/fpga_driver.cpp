// fpga_driver.cpp - ARM↔FPGA userspace driver implementation (T051)

#include "fpga_driver.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace riscv_gpgpu {
namespace fpga {

namespace {
constexpr size_t kAllocAlign = 16;

// Instruction memory occupies the bottom of the aperture; device data
// buffers are allocated above this watermark so kernel code is never
// overwritten by gpgpuMalloc().
constexpr uint64_t kDataHeapOffset = 16ULL * 1024 * 1024;  // 16 MiB
} // namespace

FpgaDriver::~FpgaDriver() { close(); }

FpgaDriver& FpgaDriver::instance() {
    static FpgaDriver driver;
    return driver;
}

bool FpgaDriver::open(const FpgaDriverConfig& config) {
    close();

    reg_fd_ = ::open(config.reg_dev_path.c_str(), O_RDWR | O_SYNC);
    if (reg_fd_ < 0) {
        std::cerr << "[fpga_driver] cannot open register device "
                  << config.reg_dev_path << ": " << std::strerror(errno) << "\n";
        return false;
    }
    void* reg_map = ::mmap(nullptr, kRegBlockSize, PROT_READ | PROT_WRITE,
                           MAP_SHARED, reg_fd_,
                           static_cast<off_t>(config.reg_map_offset));
    if (reg_map == MAP_FAILED) {
        std::cerr << "[fpga_driver] mmap of register block failed: "
                  << std::strerror(errno) << "\n";
        close();
        return false;
    }
    regs_ = static_cast<volatile uint32_t*>(reg_map);

    mem_fd_ = ::open(config.mem_dev_path.c_str(), O_RDWR | O_SYNC);
    if (mem_fd_ < 0) {
        std::cerr << "[fpga_driver] cannot open memory device "
                  << config.mem_dev_path << ": " << std::strerror(errno) << "\n";
        close();
        return false;
    }
    void* mem_map = ::mmap(nullptr, config.mem_map_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, mem_fd_,
                           static_cast<off_t>(config.mem_map_offset));
    if (mem_map == MAP_FAILED) {
        std::cerr << "[fpga_driver] mmap of memory aperture failed: "
                  << std::strerror(errno) << "\n";
        close();
        return false;
    }
    mem_ = static_cast<volatile uint8_t*>(mem_map);
    mem_base_ = config.mem_map_offset;
    mem_size_ = config.mem_map_size;
    // Reserve the bottom of the aperture for kernel code, clamped so small
    // apertures (e.g. test windows) still leave room for data buffers.
    next_alloc_ = mem_base_
                + std::min<uint64_t>(kDataHeapOffset, mem_size_ / 2);
    buffers_.clear();

    if (!config.skip_id_check) {
        const uint32_t id = readReg(REG_ID);
        if (id != kDeviceId) {
            std::cerr << "[fpga_driver] unexpected device ID 0x" << std::hex << id
                      << " (expected 0x" << kDeviceId << ")" << std::dec << "\n";
            close();
            return false;
        }
    }

    std::cout << "[fpga_driver] mapped regs via " << config.reg_dev_path
              << ", memory aperture " << (mem_size_ >> 20) << " MiB @ 0x"
              << std::hex << mem_base_ << std::dec << "\n";
    return true;
}

void FpgaDriver::close() {
    if (regs_ != nullptr) {
        ::munmap(const_cast<uint32_t*>(regs_), kRegBlockSize);
        regs_ = nullptr;
    }
    if (mem_ != nullptr) {
        ::munmap(const_cast<uint8_t*>(mem_), mem_size_);
        mem_ = nullptr;
    }
    if (reg_fd_ >= 0) { ::close(reg_fd_); reg_fd_ = -1; }
    if (mem_fd_ >= 0) { ::close(mem_fd_); mem_fd_ = -1; }
    buffers_.clear();
    mem_base_ = mem_size_ = next_alloc_ = 0;
}

uint32_t FpgaDriver::readReg(uint32_t offset) const {
    return regs_[offset / sizeof(uint32_t)];
}

void FpgaDriver::writeReg(uint32_t offset, uint32_t value) {
    regs_[offset / sizeof(uint32_t)] = value;
}

bool FpgaDriver::reset() {
    writeReg(REG_CTRL, CTRL_RESET);
    return waitForStatus(Status::IDLE, 100);
}

void FpgaDriver::start() {
    writeReg(REG_CTRL, CTRL_START);
}

Status FpgaDriver::status() const {
    return static_cast<Status>(readReg(REG_STATUS));
}

bool FpgaDriver::waitForStatus(Status expected, uint32_t timeout_ms) const {
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(timeout_ms);
    while (status() != expected) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

// ── Device memory ─────────────────────────────────────────────────────────────

volatile uint8_t* FpgaDriver::memAt(uint64_t dev_addr, size_t size) const {
    if (mem_ == nullptr) return nullptr;
    if (dev_addr < mem_base_ || dev_addr + size > mem_base_ + mem_size_) {
        std::cerr << "[fpga_driver] device address 0x" << std::hex << dev_addr
                  << " +" << std::dec << size << " outside aperture\n";
        return nullptr;
    }
    return mem_ + (dev_addr - mem_base_);
}

bool FpgaDriver::allocateBuffer(uint64_t& dev_addr, size_t size) {
    if (size == 0) {
        std::cerr << "[fpga_driver] allocateBuffer: size must be > 0\n";
        return false;
    }
    const size_t aligned = ((size + kAllocAlign - 1) / kAllocAlign) * kAllocAlign;
    if (next_alloc_ + aligned > mem_base_ + mem_size_) {
        std::cerr << "[fpga_driver] allocateBuffer: aperture exhausted\n";
        return false;
    }
    dev_addr = next_alloc_;
    next_alloc_ += aligned;
    buffers_[dev_addr] = size;
    return true;
}

bool FpgaDriver::freeBuffer(uint64_t dev_addr) {
    return buffers_.erase(dev_addr) != 0;
}

size_t FpgaDriver::bufferSize(uint64_t dev_addr) const {
    auto it = buffers_.find(dev_addr);
    return (it != buffers_.end()) ? it->second : 0;
}

bool FpgaDriver::copyToDevice(uint64_t dst_dev, const void* src_host, size_t size) {
    auto it = buffers_.find(dst_dev);
    if (it == buffers_.end() || size > it->second) {
        std::cerr << "[fpga_driver] copyToDevice: invalid buffer 0x"
                  << std::hex << dst_dev << std::dec << " size " << size << "\n";
        return false;
    }
    return writeMem(dst_dev, src_host, size);
}

bool FpgaDriver::copyFromDevice(void* dst_host, uint64_t src_dev, size_t size) {
    auto it = buffers_.find(src_dev);
    if (it == buffers_.end() || size > it->second) {
        std::cerr << "[fpga_driver] copyFromDevice: invalid buffer 0x"
                  << std::hex << src_dev << std::dec << " size " << size << "\n";
        return false;
    }
    return readMem(dst_host, src_dev, size);
}

bool FpgaDriver::writeMem(uint64_t dev_addr, const void* src_host, size_t size) {
    volatile uint8_t* dst = memAt(dev_addr, size);
    if (dst == nullptr) return false;
    std::memcpy(const_cast<uint8_t*>(dst), src_host, size);
    return true;
}

bool FpgaDriver::readMem(void* dst_host, uint64_t dev_addr, size_t size) const {
    volatile uint8_t* src = memAt(dev_addr, size);
    if (src == nullptr) return false;
    std::memcpy(dst_host, const_cast<const uint8_t*>(src), size);
    return true;
}

} // namespace fpga
} // namespace riscv_gpgpu
