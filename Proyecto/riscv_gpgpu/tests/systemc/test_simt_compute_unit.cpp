// test_simt_compute_unit.cpp - ComputeUnit integration tests for SIMT hooks

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <cstdlib>

#include "../../models/systemc/compute_unit/compute_unit.h"
#include "../../models/systemc/integration/elf_loader.h"
#include "../../models/systemc/memory/memory_hierarchy.h"
#include "../../models/systemc/simt_controller/simt_controller.h"

using namespace riscv_gpgpu;
namespace fs = std::filesystem;

static bool compileKernel(const std::string& src, const std::string& elf) {
    std::string obj = elf + ".o";
    std::string compile = "clang -target riscv32-unknown-elf"
                          " -march=rv32im -mabi=ilp32"
                          " -O0 -fno-exceptions -fomit-frame-pointer"
                          " -x c -c " + src + " -o " + obj + " 2>&1";
    int r = std::system(compile.c_str());
    if (r != 0) return false;

    std::string link = "clang -target riscv32-unknown-elf"
                       " -march=rv32im -mabi=ilp32"
                       " -fuse-ld=lld -nostdlib -Wl,--entry,0"
                       " -o " + elf + " " + obj + " 2>&1";
    r = std::system(link.c_str());
    return (r == 0) && fs::exists(elf);
}

TEST(SIMTComputeUnit, BranchExecutesWithSIMTController) {
    fs::path src = fs::temp_directory_path() / "test_simt_branch.c";
    fs::path elf = fs::temp_directory_path() / "test_simt_branch.riscv.elf";

    {
        std::ofstream f(src);
        ASSERT_TRUE(f.is_open());
                f << "volatile int sink;\n"
                    << "int branchy(int x) { if (x < 5) sink = x + 1; else sink = x + 2; return sink; }\n";
    }

    ASSERT_TRUE(compileKernel(src.string(), elf.string()));

    MemoryHierarchy::Config mem_cfg;
    mem_cfg.shared_mem_size = 4096;
    mem_cfg.global_mem_size = 0;
    mem_cfg.cache_line_size = 64;
    mem_cfg.l1_cache_size = 16384;
    mem_cfg.l2_cache_size = 262144;
    MemoryHierarchy mem("simt_mem", mem_cfg);

    ElfLoader loader;
    ASSERT_TRUE(loader.load(elf.string(), mem));

    SymbolEntry sym;
    ASSERT_TRUE(loader.findSymbol("branchy", sym));

    SIMTController::Config simt_cfg{
        SIMTController::RecovergenceMode::IMMEDIATE,
        true,
        32
    };
    SIMTController simt("simt_ctrl", simt_cfg);

    ComputeUnit::Config cu_cfg;
    cu_cfg.unit_id = 0;
    cu_cfg.num_threads = 1;
    cu_cfg.threads_per_warp = 1;
    cu_cfg.max_warps = 1;
    cu_cfg.shared_mem_size = 4096;
    cu_cfg.max_cycles = 100000;

    ComputeUnit cu("simt_cu", cu_cfg);
    cu.setMemoryHierarchy(&mem);
    cu.setSIMTController(&simt);
    cu.setEntryPoint(sym.address);

    std::array<uint32_t, 32> regs{};
    regs[2] = 0x20000000u;
    regs[1] = 0x00000001u;
    regs[10] = 3;
    cu.setInitialRegisters(regs);
    cu.setReturnSentinel(0x00000001u);
    cu.launchKernel(0, 1, 1);

    while (!cu.isComplete()) {
        cu.step();
    }

    EXPECT_EQ(simt.getTotalDivergenceEvents(), 0u);
    EXPECT_EQ(cu.getRegister(0, 10), 4u);

    fs::remove(src);
    fs::remove(elf);
    fs::remove(elf.string() + ".o");
}