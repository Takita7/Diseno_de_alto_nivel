// fpga_elf_loader.cpp - Load RISC-V ELF32 kernels into FPGA memory (T052)
//
// Counterpart of models/systemc/integration/elf_loader.cpp: instead of
// writing PT_LOAD segments into the SystemC MemoryHierarchy, segments are
// DMA-transferred into the FPGA global memory aperture and REG_PC_INIT is
// written with the ELF entry point. Manual ELF32 parsing, no libelf.

#include "fpga_elf_loader.h"

#include <cstring>
#include <fstream>
#include <iostream>

namespace riscv_gpgpu {
namespace fpga {

namespace {

// ── Minimal ELF32 structures (little-endian RISC-V) ─────────────────────────
#pragma pack(push, 1)
struct Elf32Header {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};
struct Elf32ProgramHeader {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};
#pragma pack(pop)

constexpr uint32_t PT_LOAD    = 1;
constexpr uint8_t  ELFCLASS32 = 1;
constexpr uint16_t EM_RISCV   = 243;
constexpr size_t   kVerifyBytes = 16;

} // namespace

bool loadElfToFpga(FpgaDriver& driver,
                   const std::vector<uint8_t>& elf_bytes,
                   uint32_t& entry_point) {
    if (!driver.isOpen()) {
        std::cerr << "[fpga_elf_loader] driver is not open\n";
        return false;
    }
    if (elf_bytes.size() < sizeof(Elf32Header)) {
        std::cerr << "[fpga_elf_loader] image too small for an ELF header\n";
        return false;
    }

    Elf32Header hdr;
    std::memcpy(&hdr, elf_bytes.data(), sizeof(hdr));
    if (hdr.e_ident[0] != 0x7F || hdr.e_ident[1] != 'E'
        || hdr.e_ident[2] != 'L' || hdr.e_ident[3] != 'F') {
        std::cerr << "[fpga_elf_loader] not a valid ELF image\n";
        return false;
    }
    if (hdr.e_ident[4] != ELFCLASS32) {
        std::cerr << "[fpga_elf_loader] only ELF32 is supported\n";
        return false;
    }
    if (hdr.e_machine != EM_RISCV) {
        std::cerr << "[fpga_elf_loader] not a RISC-V ELF (e_machine="
                  << hdr.e_machine << ")\n";
        return false;
    }
    if (hdr.e_phoff == 0 || hdr.e_phnum == 0) {
        std::cerr << "[fpga_elf_loader] ELF has no program headers\n";
        return false;
    }

    std::cout << "[fpga_elf_loader] e_entry=0x" << std::hex << hdr.e_entry
              << std::dec << "  segments=" << hdr.e_phnum << "\n";

    for (uint16_t i = 0; i < hdr.e_phnum; ++i) {
        const size_t ph_off = hdr.e_phoff
                            + static_cast<size_t>(i) * hdr.e_phentsize;
        if (ph_off + sizeof(Elf32ProgramHeader) > elf_bytes.size()) {
            std::cerr << "[fpga_elf_loader] program header " << i
                      << " out of bounds\n";
            return false;
        }
        Elf32ProgramHeader ph;
        std::memcpy(&ph, elf_bytes.data() + ph_off, sizeof(ph));
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;
        if (ph.p_offset + static_cast<uint64_t>(ph.p_filesz) > elf_bytes.size()) {
            std::cerr << "[fpga_elf_loader] segment " << i
                      << " file range out of bounds\n";
            return false;
        }

        std::cout << "[fpga_elf_loader]   PT_LOAD vaddr=0x" << std::hex
                  << ph.p_vaddr << std::dec << " filesz=" << ph.p_filesz
                  << " memsz=" << ph.p_memsz << "\n";

        // DMA the file-backed bytes to the segment's virtual address.
        if (ph.p_filesz > 0
            && !driver.writeMem(ph.p_vaddr,
                                elf_bytes.data() + ph.p_offset, ph.p_filesz)) {
            std::cerr << "[fpga_elf_loader] DMA of segment " << i << " failed\n";
            return false;
        }
        // Zero-fill .bss (memsz > filesz).
        if (ph.p_memsz > ph.p_filesz) {
            const std::vector<uint8_t> zeros(ph.p_memsz - ph.p_filesz, 0);
            if (!driver.writeMem(ph.p_vaddr + ph.p_filesz,
                                 zeros.data(), zeros.size())) {
                std::cerr << "[fpga_elf_loader] BSS zero-fill of segment "
                          << i << " failed\n";
                return false;
            }
        }

        // Verification: read back the first bytes and compare (T052 spec).
        const size_t verify = std::min<size_t>(kVerifyBytes, ph.p_filesz);
        if (verify > 0) {
            uint8_t readback[kVerifyBytes] = {};
            if (!driver.readMem(readback, ph.p_vaddr, verify)
                || std::memcmp(readback, elf_bytes.data() + ph.p_offset,
                               verify) != 0) {
                std::cerr << "[fpga_elf_loader] read-back verification failed "
                             "for segment " << i << "\n";
                return false;
            }
        }
    }

    driver.writeReg(REG_PC_INIT, hdr.e_entry);
    entry_point = hdr.e_entry;
    std::cout << "[fpga_elf_loader] PC_INIT=0x" << std::hex << hdr.e_entry
              << std::dec << " written\n";
    return true;
}

bool loadElfFileToFpga(FpgaDriver& driver,
                       const std::string& path,
                       uint32_t& entry_point) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "[fpga_elf_loader] cannot open: " << path << "\n";
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
    return loadElfToFpga(driver, bytes, entry_point);
}

} // namespace fpga
} // namespace riscv_gpgpu
