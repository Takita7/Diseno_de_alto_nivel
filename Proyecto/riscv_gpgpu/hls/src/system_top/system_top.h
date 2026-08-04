// system_top.h — Multi-device HLS coordinator (host-side, not synthesizable).
//
// Mirrors models/systemc/src/system_top/system_top.h API.
// Each "device" is one gpgpu_scheduler IP instance accessible via a
// DeviceInterface. The warp split algorithm is identical to SystemTop's:
//
//   base      = total_warps / num_devices
//   remainder = total_warps % num_devices
//   device d  gets (base + (d < remainder ? 1 : 0)) warps
//   device d  warp_id_offset = sum of warps for devices 0..d-1
//
// `initial_regs_ptr` passed to each gpgpu_scheduler must be the SAME
// global-warp-id-indexed buffer (docs/hls/interfaces.md SS16); each device
// reads only its own subset by following the global warp_id (offset
// included). The host allocates one contiguous buffer covering all warps.
//
// This class is plain C++17 — no HLS pragmas, no SystemC, no ap_int.
// Intended for use in driver/ and test code, not for HLS synthesis.

#ifndef RISCV_GPGPU_HLS_SYSTEM_TOP_H
#define RISCV_GPGPU_HLS_SYSTEM_TOP_H

#include <cstdint>
#include <memory>
#include <vector>

namespace riscv_gpgpu_hls {

// Abstract control interface for one gpgpu_scheduler AXI-Lite register map.
// Subclass to target a real FPGA driver (UIO/XDMA) or a test mock.
struct DeviceInterface {
    virtual ~DeviceInterface() = default;

    virtual void writeWarpIdOffset(uint32_t offset) = 0;
    virtual void writeTotalWarps(uint32_t n)        = 0;
    virtual void writeProgramLen(uint32_t len)      = 0;
    virtual void writeStart(bool v)                 = 0;
    virtual bool readBusy()  const                  = 0;
    virtual bool readDone()  const                  = 0;
    virtual bool readFault() const                  = 0;
    // Sum of warp_status_t::instr_count across all completed warps on this device.
    virtual uint64_t readInstructionsRetired() const = 0;
};

class SystemTopHLS {
public:
    struct Config {
        uint32_t num_devices = 2;
    };

    SystemTopHLS(const Config& cfg,
                 std::vector<std::unique_ptr<DeviceInterface>> devices)
        : config_(cfg), devices_(std::move(devices)) {}

    // Split total_warps across devices using the same algorithm as SystemTop
    // (models/systemc/src/system_top/system_top.h). Writes warp_id_offset,
    // total_warps, program_len to each device then asserts start.
    void launchKernel(uint32_t total_warps, uint32_t program_len) {
        uint32_t n_dev  = static_cast<uint32_t>(devices_.size());
        uint32_t base   = total_warps / n_dev;
        uint32_t rem    = total_warps % n_dev;
        uint32_t offset = 0;
        for (uint32_t d = 0; d < n_dev; ++d) {
            uint32_t n = base + (d < rem ? 1u : 0u);
            if (n == 0) continue;
            devices_[d]->writeWarpIdOffset(offset);
            devices_[d]->writeTotalWarps(n);
            devices_[d]->writeProgramLen(program_len);
            devices_[d]->writeStart(true);
            offset += n;
        }
    }

    // True once every device reports done.
    bool isComplete() const {
        for (const auto& dev : devices_) {
            if (!dev->readDone()) return false;
        }
        return true;
    }

    bool isFault() const {
        for (const auto& dev : devices_) {
            if (dev->readFault()) return true;
        }
        return false;
    }

    uint32_t    getNumDevices()         const { return static_cast<uint32_t>(devices_.size()); }
    DeviceInterface& getDevice(uint32_t i)    { return *devices_[i]; }

    // Aggregated across all devices.
    uint64_t getTotalInstructions() const {
        uint64_t total = 0;
        for (const auto& dev : devices_) total += dev->readInstructionsRetired();
        return total;
    }

private:
    Config                                      config_;
    std::vector<std::unique_ptr<DeviceInterface>> devices_;
};

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_SYSTEM_TOP_H
