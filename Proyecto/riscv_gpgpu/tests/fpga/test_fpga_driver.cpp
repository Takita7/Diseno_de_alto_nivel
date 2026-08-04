// test_fpga_driver.cpp - Unit tests for the ARM↔FPGA userspace driver (T051)
// and the FPGA ELF loader (T052).
//
// On x86 there is no Kria hardware, so the tests exercise the driver against
// file-backed fake windows: ordinary temp files stand in for the UIO register
// block and the /dev/mem global memory aperture. The mmap/register/DMA logic
// under test is identical to what runs on the board; only the device paths
// differ (see FpgaDriverConfig).

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

#include "../../driver/src/fpga_driver.h"
#include "../../driver/src/fpga_elf_loader.h"
#include "../../driver/src/fpga_regs.h"

using namespace riscv_gpgpu::fpga;

namespace {

constexpr size_t kFakeMemSize = 1024 * 1024;  // 1 MiB fake aperture

class FpgaDriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        reg_path_ = makeTempFile("gpgpu_regs", kRegBlockSize);
        mem_path_ = makeTempFile("gpgpu_mem", kFakeMemSize);
        // Pre-program the fake ID register so the sanity check passes.
        std::fstream regs(reg_path_, std::ios::in | std::ios::out | std::ios::binary);
        const uint32_t id = kDeviceId;
        regs.seekp(REG_ID);
        regs.write(reinterpret_cast<const char*>(&id), sizeof(id));

        config_.reg_dev_path = reg_path_;
        config_.reg_map_offset = 0;
        config_.mem_dev_path = mem_path_;
        config_.mem_map_offset = 0;
        config_.mem_map_size = kFakeMemSize;
    }

    void TearDown() override {
        driver_.close();
        ::unlink(reg_path_.c_str());
        ::unlink(mem_path_.c_str());
    }

    static std::string makeTempFile(const std::string& tag, size_t size) {
        std::string tmpl = std::string(::getenv("TMPDIR") ? ::getenv("TMPDIR") : "/tmp")
                         + "/" + tag + ".XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
        int fd = ::mkstemp(buf.data());
        EXPECT_GE(fd, 0);
        EXPECT_EQ(::ftruncate(fd, static_cast<off_t>(size)), 0);
        ::close(fd);
        return std::string(buf.data());
    }

    FpgaDriver driver_;
    FpgaDriverConfig config_;
    std::string reg_path_;
    std::string mem_path_;
};

// ── T051 verification: map registers and read STATUS == IDLE after reset ────

TEST_F(FpgaDriverTest, OpenMapsRegistersAndStatusIsIdleAfterReset) {
    ASSERT_TRUE(driver_.open(config_));
    ASSERT_TRUE(driver_.isOpen());
    EXPECT_EQ(driver_.readReg(REG_ID), kDeviceId);

    // The fake window has no hardware to self-clear CTRL, but STATUS is
    // zero-initialized == IDLE, which is the T051 post-reset expectation.
    driver_.writeReg(REG_CTRL, CTRL_RESET);
    EXPECT_EQ(driver_.status(), Status::IDLE);
}

TEST_F(FpgaDriverTest, OpenFailsOnWrongDeviceId) {
    // Corrupt the ID register.
    std::fstream regs(reg_path_, std::ios::in | std::ios::out | std::ios::binary);
    const uint32_t bogus = 0xDEADBEEF;
    regs.seekp(REG_ID);
    regs.write(reinterpret_cast<const char*>(&bogus), sizeof(bogus));
    regs.flush();
    EXPECT_FALSE(driver_.open(config_));
    EXPECT_FALSE(driver_.isOpen());
}

TEST_F(FpgaDriverTest, RegisterWritesAreVisibleOnReadback) {
    ASSERT_TRUE(driver_.open(config_));
    driver_.writeReg(REG_GRID_X, 64);
    driver_.writeReg(REG_GRID_Y, 2);
    driver_.writeReg(REG_PC_INIT, 0x1000);
    driver_.writeReg(REG_IRQ_ENABLE, IRQ_ENABLE_DONE);
    EXPECT_EQ(driver_.readReg(REG_GRID_X), 64u);
    EXPECT_EQ(driver_.readReg(REG_GRID_Y), 2u);
    EXPECT_EQ(driver_.readReg(REG_PC_INIT), 0x1000u);
    EXPECT_EQ(driver_.readReg(REG_IRQ_ENABLE), IRQ_ENABLE_DONE);
}

TEST_F(FpgaDriverTest, BufferAllocateCopyRoundtrip) {
    ASSERT_TRUE(driver_.open(config_));
    uint64_t dev = 0;
    ASSERT_TRUE(driver_.allocateBuffer(dev, 256));
    EXPECT_EQ(driver_.bufferSize(dev), 256u);

    std::vector<uint8_t> src(256);
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>(i * 7);
    ASSERT_TRUE(driver_.copyToDevice(dev, src.data(), src.size()));

    std::vector<uint8_t> dst(256, 0);
    ASSERT_TRUE(driver_.copyFromDevice(dst.data(), dev, dst.size()));
    EXPECT_EQ(src, dst);

    EXPECT_TRUE(driver_.freeBuffer(dev));
    EXPECT_FALSE(driver_.copyToDevice(dev, src.data(), src.size()));
}

TEST_F(FpgaDriverTest, CopyRejectsUnknownAddressAndOversizedCopy) {
    ASSERT_TRUE(driver_.open(config_));
    uint8_t byte = 0;
    EXPECT_FALSE(driver_.copyToDevice(0xDEAD0000, &byte, 1));
    uint64_t dev = 0;
    ASSERT_TRUE(driver_.allocateBuffer(dev, 16));
    std::vector<uint8_t> big(32, 0);
    EXPECT_FALSE(driver_.copyToDevice(dev, big.data(), big.size()));
}

// ── T052 verification: ELF PT_LOAD → FPGA memory + PC_INIT + read-back ──────

namespace elfimg {

// Build a minimal RISC-V ELF32 with one PT_LOAD segment at vaddr 0x1000.
std::vector<uint8_t> makeElf(uint32_t entry, uint32_t vaddr,
                             const std::vector<uint8_t>& payload,
                             uint32_t memsz_extra = 0) {
    std::vector<uint8_t> img(0x1000 + payload.size(), 0);
    auto w16 = [&](size_t off, uint16_t v) { std::memcpy(&img[off], &v, 2); };
    auto w32 = [&](size_t off, uint32_t v) { std::memcpy(&img[off], &v, 4); };

    // ELF header
    img[0] = 0x7F; img[1] = 'E'; img[2] = 'L'; img[3] = 'F';
    img[4] = 1;  // ELFCLASS32
    img[5] = 1;  // little-endian
    img[6] = 1;  // EV_CURRENT
    w16(0x10, 2);      // e_type = EXEC
    w16(0x12, 243);    // e_machine = EM_RISCV
    w32(0x14, 1);      // e_version
    w32(0x18, entry);  // e_entry
    w32(0x1C, 0x34);   // e_phoff (right after ELF header)
    w16(0x28, 0x34);   // e_ehsize
    w16(0x2A, 0x20);   // e_phentsize
    w16(0x2C, 1);      // e_phnum

    // Program header @0x34
    w32(0x34, 1);                     // p_type = PT_LOAD
    w32(0x38, 0x1000);                // p_offset
    w32(0x3C, vaddr);                 // p_vaddr
    w32(0x40, vaddr);                 // p_paddr
    w32(0x44, static_cast<uint32_t>(payload.size()));               // p_filesz
    w32(0x48, static_cast<uint32_t>(payload.size()) + memsz_extra); // p_memsz
    w32(0x4C, 5);                     // p_flags = R+X
    w32(0x50, 4);                     // p_align

    std::memcpy(&img[0x1000], payload.data(), payload.size());
    return img;
}

} // namespace elfimg

TEST_F(FpgaDriverTest, ElfLoaderWritesSegmentAndPcInit) {
    ASSERT_TRUE(driver_.open(config_));
    std::vector<uint8_t> code(64);
    for (size_t i = 0; i < code.size(); ++i) code[i] = static_cast<uint8_t>(0xA0 + i);
    const uint32_t entry = 0x1010;
    const auto elf = elfimg::makeElf(entry, 0x1000, code, /*bss*/ 32);

    uint32_t reported_entry = 0;
    ASSERT_TRUE(loadElfToFpga(driver_, elf, reported_entry));
    EXPECT_EQ(reported_entry, entry);
    EXPECT_EQ(driver_.readReg(REG_PC_INIT), entry);

    // Read back the first 16 bytes of instruction memory and compare (T052).
    uint8_t readback[16] = {};
    ASSERT_TRUE(driver_.readMem(readback, 0x1000, sizeof(readback)));
    EXPECT_EQ(std::memcmp(readback, code.data(), sizeof(readback)), 0);

    // BSS must be zero-filled.
    uint8_t bss[32];
    std::memset(bss, 0xFF, sizeof(bss));
    ASSERT_TRUE(driver_.readMem(bss, 0x1000 + code.size(), sizeof(bss)));
    for (uint8_t b : bss) EXPECT_EQ(b, 0);
}

TEST_F(FpgaDriverTest, ElfLoaderRejectsInvalidImages) {
    ASSERT_TRUE(driver_.open(config_));
    uint32_t entry = 0;

    std::vector<uint8_t> not_elf(64, 0);
    EXPECT_FALSE(loadElfToFpga(driver_, not_elf, entry));

    // Valid magic but wrong machine (x86 = 3).
    auto elf = elfimg::makeElf(0x1000, 0x1000, std::vector<uint8_t>(16, 0x13));
    elf[0x12] = 3; elf[0x13] = 0;
    EXPECT_FALSE(loadElfToFpga(driver_, elf, entry));
}

} // namespace
