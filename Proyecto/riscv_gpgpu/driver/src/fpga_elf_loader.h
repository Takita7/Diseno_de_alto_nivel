// fpga_elf_loader.h - Load RISC-V ELF32 kernels into FPGA memory (T052)

#ifndef RISCV_GPGPU_FPGA_ELF_LOADER_H
#define RISCV_GPGPU_FPGA_ELF_LOADER_H

#include <cstdint>
#include <string>
#include <vector>

#include "fpga_driver.h"

namespace riscv_gpgpu {
namespace fpga {

// Parse an ELF32 image, DMA every PT_LOAD segment to its virtual address in
// the FPGA global memory aperture, verify the first 16 bytes of each segment
// by read-back, and write REG_PC_INIT with the ELF entry point.
// On success, entry_point receives e_entry.
bool loadElfToFpga(FpgaDriver& driver,
                   const std::vector<uint8_t>& elf_bytes,
                   uint32_t& entry_point);

// Convenience wrapper: read the ELF from `path` and call loadElfToFpga().
bool loadElfFileToFpga(FpgaDriver& driver,
                       const std::string& path,
                       uint32_t& entry_point);

} // namespace fpga
} // namespace riscv_gpgpu

#endif // RISCV_GPGPU_FPGA_ELF_LOADER_H
