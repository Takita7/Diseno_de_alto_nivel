// top_test.cpp – Phase 0 through Phase 6
//

#include <systemc>
#include <iostream>
#include <set>

#include "top/top.h"
#include "memory/memory_hierarchy.h"
#include "scheduler/warp_scheduler.h"
#include "simt_controller/simt_controller.h"
#include "compute_unit/compute_unit.h"
#include "common/platform.h"
#include "common/logging.h"

using namespace riscv_gpgpu;

#define CHECK(cond, msg) \
    do { \
        if (cond) { std::cout << "[PASS] " << (msg) << "\n"; } \
        else      { std::cout << "[FAIL] " << (msg) << "\n"; overall_pass = false; } \
    } while (0)

static WarpContext makeContext(WarpID warp_id, uint32_t threads,
                               std::vector<Instruction> prog) {
    WarpContext ctx;
    ctx.warp_id = warp_id;
    ctx.regs.resize(threads, std::vector<uint32_t>(32, 0));
    ctx.program = std::move(prog);
    return ctx;
}

int sc_main(int /*argc*/, char* /*argv*/[]) {
    bool overall_pass = true;
    Platform::printSimulationBanner();

    // ── Instantiate all modules before sc_start ───────────────────────────────

    GPGPUTop::Config top_config;
    top_config.num_compute_units = 1;
    top_config.max_warps_per_cu  = 4;
    top_config.threads_per_warp  = 32;
    top_config.shared_mem_size   = 16 * 1024;
    top_config.l1_cache_size     = 32 * 1024;
    top_config.l2_cache_size     = 512 * 1024;
    GPGPUTop top("gpgpu_top", top_config);

    sc_core::sc_clock test_clock("test_clock",
        sc_core::sc_time(GPGPU_CLOCK_PERIOD_NS, sc_core::SC_NS));

    MemoryHierarchy::Config mem_config;
    mem_config.shared_mem_size = 16 * 1024;
    mem_config.l1_cache_size   = 32 * 1024;
    mem_config.l2_cache_size   = 512 * 1024;
    mem_config.cache_line_size = 128;
    MemoryHierarchy mem_test("mem_test", mem_config);
    mem_test.clk(test_clock);

    WarpScheduler::Config sched1_config;
    sched1_config.num_compute_units = 1;
    sched1_config.max_warps_per_cu  = 8;
    sched1_config.policy = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    WarpScheduler sched1("sched1", sched1_config);
    sched1.clk(test_clock);

    WarpScheduler::Config sched2_config;
    sched2_config.num_compute_units = 2;
    sched2_config.max_warps_per_cu  = 8;
    sched2_config.policy = WarpScheduler::SchedulingPolicy::ROUND_ROBIN;
    WarpScheduler sched2("sched2", sched2_config);
    sched2.clk(test_clock);

    SIMTController::Config simt_config;
    simt_config.mode = SIMTController::RecovergenceMode::IMMEDIATE;
    SIMTController simt("simt_test", simt_config);
    simt.clk(test_clock);

    ComputeUnit::Config cu_config;
    cu_config.unit_id          = 99;
    cu_config.threads_per_warp = 4;
    cu_config.num_threads      = 4;
    cu_config.max_warps        = 4;
    cu_config.shared_mem_size  = 4 * 1024;
    ComputeUnit cu_test("cu_test", cu_config);
    cu_test.clk(test_clock);
    // cu_test uses sim_memory_ (no setMemory call) – suitable for register-based tests

    sc_core::sc_start(sc_core::sc_time(0, sc_core::SC_NS));

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 0
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(0, "Build & Initialization");
    LOG_SEP("Phase 0 Results");
    CHECK(top.isKernelComplete(), "GPGPUTop: isKernelComplete() before launch");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 1 – Memory Hierarchy
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(1, "Memory Hierarchy");
    uint32_t data = 0, latency = 0;

    LOG_SEP("1a: Global memory");
    mem_test.storeWord(0x10000, 0xDEADBEEF, latency);
    mem_test.loadWord (0x10000, data, latency);
    CHECK(data == 0xDEADBEEF, "Write→read: 0xDEADBEEF");

    LOG_SEP("1b: L1 hit");
    uint64_t hits_before = mem_test.getL1CacheHits();
    mem_test.loadWord(0x10000, data, latency);
    CHECK(mem_test.getL1CacheHits() == hits_before + 1, "L1 hit counter incremented");
    CHECK(latency == 1, "L1 hit latency == 1");

    LOG_SEP("1c: Cache invalidation");
    mem_test.invalidateCache();
    uint64_t misses_before = mem_test.getL1CacheMisses();
    mem_test.loadWord(0x10000, data, latency);
    CHECK(mem_test.getL1CacheMisses() == misses_before + 1, "L1 miss after invalidation");
    CHECK(data == 0xDEADBEEF, "Data correct after re-fetch");

    LOG_SEP("1d: Shared memory");
    mem_test.storeSharedMemory(0x0, 0x12345678);
    data = 0;
    mem_test.loadSharedMemory(0x0, data);
    CHECK(data == 0x12345678, "Shared memory write→read");

    LOG_SEP("1e: No aliasing");
    mem_test.storeWord(0x20000, 0xCAFEBABE, latency);
    mem_test.storeWord(0x30000, 0xDEADC0DE, latency);
    uint32_t d1 = 0, d2 = 0;
    mem_test.loadWord(0x20000, d1, latency);
    mem_test.loadWord(0x30000, d2, latency);
    CHECK(d1 == 0xCAFEBABE, "0x20000 correct");
    CHECK(d2 == 0xDEADC0DE, "0x30000 correct");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 2 – Warp Scheduler
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(2, "Warp Scheduler");

    LOG_SEP("2a: Basic dispatch");
    sched1.submitKernel(0, 2, 2);
    CHECK(sched1.hasReadyWarps(0), "hasReadyWarps after submit");
    std::set<WarpID> seen;
    bool all_valid = true;
    for (int i = 0; i < 4; ++i) {
        WarpID w = sched1.selectWarp(0);
        if (w == WarpScheduler::INVALID_WARP_ID) { all_valid = false; break; }
        seen.insert(w);
    }
    CHECK(all_valid, "All IDs valid");
    CHECK(seen.size() == 4, "All IDs distinct");
    for (WarpID w : seen) sched1.markWarpComplete(0, w);
    CHECK(sched1.isComplete(), "isComplete after all warps done");

    LOG_SEP("2b: Stall");
    sched1.submitKernel(0, 1, 2);
    WarpID wa = sched1.selectWarp(0), wb = sched1.selectWarp(0);
    sched1.markWarpStalled(0, wa);
    sched1.markWarpComplete(0, wb);
    CHECK(!sched1.isComplete(), "isComplete=false with stalled warp");

    LOG_SEP("2c: Load balance");
    sched2.submitKernel(0, 2, 2);
    uint32_t cu0 = 0, cu1 = 0;
    while (sched2.hasReadyWarps(0)) { sched2.selectWarp(0); ++cu0; }
    while (sched2.hasReadyWarps(1)) { sched2.selectWarp(1); ++cu1; }
    CHECK(cu0 == 2, "CU0 = 2 warps");
    CHECK(cu1 == 2, "CU1 = 2 warps");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 3 – SIMT Controller
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(3, "SIMT Controller");

    LOG_SEP("3a: Init");
    simt.initializeWarp(0, 8);
    CHECK(simt.getActiveMask(0) == 0xFF, "Initial mask 0xFF");

    LOG_SEP("3b: Divergent branch");
    bool cond_b[8] = {true,true,true,true,false,false,false,false};
    simt.handleBranch(0, cond_b);
    CHECK(simt.getActiveMask(0) == 0x0F, "Taken mask 0x0F");
    CHECK(simt.getTotalDivergenceEvents() == 1, "1 divergence event");

    LOG_SEP("3c: Reconvergence");
    simt.handleJoin(0);
    CHECK(simt.getActiveMask(0) == 0xFF, "Mask restored 0xFF");

    LOG_SEP("3d: No-divergence");
    simt.initializeWarp(1, 4);
    bool cond_d[4] = {true,true,true,true};
    simt.handleBranch(1, cond_d);
    CHECK(simt.getTotalDivergenceEvents() == 1, "Still 1 event");

    LOG_SEP("3e: Nested divergence");
    simt.initializeWarp(2, 8);
    bool co[8] = {true,true,true,true,false,false,false,false};
    bool ci[8] = {true,true,false,false,false,false,false,false};
    simt.handleBranch(2, co);
    simt.handleBranch(2, ci);
    CHECK(simt.getActiveMask(2) == 0x03, "Inner mask 0x03");
    simt.handleJoin(2);
    simt.handleJoin(2);
    CHECK(simt.getActiveMask(2) == 0xFF, "Fully reconverged 0xFF");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 4 – Compute Unit
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(4, "Compute Unit");

    LOG_SEP("4a: Integer ALU");
    {
        WarpContext ctx = makeContext(0, 4, {
            makeInstr(Opcode::ADDI, 1, 0, 0,  5),
            makeInstr(Opcode::ADD,  2, 1, 1,  0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) ctx.regs[t][0] = t;
        cu_test.executeWarp(ctx);
        CHECK(ctx.state == WarpState::COMPLETE, "Warp COMPLETE");
        CHECK(ctx.regs[0][2] == 10, "Thread 0: r2=10");
        CHECK(ctx.regs[1][2] == 12, "Thread 1: r2=12");
        CHECK(ctx.regs[2][2] == 14, "Thread 2: r2=14");
        CHECK(ctx.regs[3][2] == 16, "Thread 3: r2=16");
    }

    LOG_SEP("4b: Vector SAXPY");
    {
        WarpContext ctx = makeContext(1, 4, {
            makeInstr(Opcode::VMUL, 3, 1, 0, 0),
            makeInstr(Opcode::VADD, 3, 3, 2, 0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][0] = 2; ctx.regs[t][1] = t+1; ctx.regs[t][2] = 10;
        }
        cu_test.executeWarp(ctx);
        CHECK(ctx.regs[0][3] == 12, "Thread 0: 2*1+10=12");
        CHECK(ctx.regs[1][3] == 14, "Thread 1: 2*2+10=14");
        CHECK(ctx.regs[2][3] == 16, "Thread 2: 2*3+10=16");
        CHECK(ctx.regs[3][3] == 18, "Thread 3: 2*4+10=18");
    }

    LOG_SEP("4c: Memory SW + LW");
    {
        WarpContext ctx = makeContext(2, 4, {
            makeInstr(Opcode::SW,  0, 0, 1, 0),
            makeInstr(Opcode::LW,  2, 0, 0, 0),
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][0] = 0x1000 + t * 4;
            ctx.regs[t][1] = t * 100;
        }
        cu_test.executeWarp(ctx);
        CHECK(ctx.regs[0][2] ==   0, "Thread 0: r2=0");
        CHECK(ctx.regs[1][2] == 100, "Thread 1: r2=100");
        CHECK(ctx.regs[2][2] == 200, "Thread 2: r2=200");
        CHECK(ctx.regs[3][2] == 300, "Thread 3: r2=300");
    }

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 5 – Top Integration  (updated: launchKernel now takes a program)
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(5, "Top Integration");

    // SAXPY kernel: uses buildWarpContext's r0=0, r1=tid, r2=mem_addr
    //   r3 = alpha=2, r4 = x[t]=tid+1, r5 = y=10
    //   r6 = alpha*x + y = 2*(tid+1)+10
    // 6 instructions/warp (ADDI×3 + VMUL + VADD + HALT)
    std::vector<Instruction> saxpy_prog = {
        makeInstr(Opcode::ADDI, 3, 0, 0,  2),   // r3 = alpha = 2
        makeInstr(Opcode::ADDI, 4, 1, 0,  1),   // r4 = tid + 1  (x[t])
        makeInstr(Opcode::ADDI, 5, 0, 0, 10),   // r5 = y = 10
        makeInstr(Opcode::VMUL, 6, 4, 3,  0),   // r6 = x * alpha
        makeInstr(Opcode::VADD, 6, 6, 5,  0),   // r6 = x*alpha + y
        makeInstr(Opcode::HALT)
    };

    LOG_SEP("5a: 2x1 kernel (2 warps)");
    CHECK(top.isKernelComplete(), "isKernelComplete() = true before launch");
    top.launchKernel(2, 1, saxpy_prog);
    CHECK(!top.isKernelComplete(), "isKernelComplete() = false after launch");
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
    CHECK(top.isKernelComplete(), "isKernelComplete() = true after sc_start");
    // 2 warps × 6 instructions = 12
    CHECK(top.getTotalInstructions() == 12,
          "getTotalInstructions() = 12  (2 warps × 6)");

    LOG_SEP("5b: Second kernel (1 warp, cumulative check)");
    top.launchKernel(1, 1, saxpy_prog);
    sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));
    CHECK(top.isKernelComplete(), "isKernelComplete() after second kernel");
    // 12 + 6 = 18
    CHECK(top.getTotalInstructions() == 18,
          "getTotalInstructions() = 18 cumulative (12 + 6)");

    // ═════════════════════════════════════════════════════════════════════════
    // Phase 6 – Benchmarks
    // ═════════════════════════════════════════════════════════════════════════
    Platform::printPhaseHeader(6, "Benchmarks");

    // ── 6a: SAXPY with real L1/L2 cache (via top) ─────────────────────────────
    // Uses r2 (unique per-thread address from buildWarpContext) for SW/LW.
    // SW is write-through/no-write-allocate → LW (first) misses L1, fills it.
    // LW (second) hits L1.
    LOG_SEP("6a: SAXPY with real memory – L1 miss then hit");
    {
        std::vector<Instruction> mem_saxpy = {
            makeInstr(Opcode::ADDI, 3, 0, 0,  2),   // r3 = alpha = 2
            makeInstr(Opcode::ADDI, 4, 1, 0,  1),   // r4 = tid + 1
            makeInstr(Opcode::ADDI, 5, 0, 0, 10),   // r5 = y = 10
            makeInstr(Opcode::VMUL, 6, 4, 3,  0),   // r6 = alpha * x
            makeInstr(Opcode::VADD, 6, 6, 5,  0),   // r6 = result
            makeInstr(Opcode::SW,   0, 2, 6,  0),   // mem[r2] = r6  (write-through)
            makeInstr(Opcode::LW,   7, 2, 0,  0),   // r7 = mem[r2]  (L1 miss → fill)
            makeInstr(Opcode::LW,   8, 2, 0,  0),   // r8 = mem[r2]  (L1 hit)
            makeInstr(Opcode::HALT)
        };
        // 9 instructions × 1 warp

        uint64_t misses_before = top.getL1CacheMisses();
        uint64_t hits_before   = top.getL1CacheHits();

        top.launchKernel(1, 1, mem_saxpy);
        sc_core::sc_start(sc_core::sc_time(100, sc_core::SC_NS));

        CHECK(top.isKernelComplete(), "Kernel complete");
        // 32 threads × 1 miss each (first LW) = 32 new misses
        CHECK(top.getL1CacheMisses() == misses_before + 32,
              "L1 misses += 32 (one per thread, first LW)");
        // 32 threads × 1 hit each (second LW) = 32 new hits
        CHECK(top.getL1CacheHits() == hits_before + 32,
              "L1 hits += 32 (one per thread, second LW)");
        CHECK(top.getDivergenceEvents() == 0,
              "No divergence in SAXPY");
        // Cumulative: 18 + 9 = 27
        CHECK(top.getTotalInstructions() == 27,
              "getTotalInstructions() = 27 cumulative");
    }

    // ── 6b: Divergent kernel – VBRANCH / VJOIN (via cu_test) ──────────────────
    // r0[t] = t  →  thread 0 (r0=0) falls through VBRANCH
    //              threads 1,2,3 (r0≠0) are masked until VJOIN
    LOG_SEP("6b: Divergent kernel – thread 0 falls through, threads 1-3 masked");
    {
        WarpContext ctx = makeContext(3, 4, {
            makeInstr(Opcode::VBRANCH, 0, 0, 0, 3),  // mask threads where r0≠0; skip to VJOIN
            makeInstr(Opcode::ADDI,    1, 0, 0, 100), // thread 0 only: r1 = 0+100 = 100
            makeInstr(Opcode::ADD,     2, 1, 0, 0),   // thread 0 only: r2 = r1+r0 = 100
            makeInstr(Opcode::VJOIN,   0, 0, 0, 0),   // all threads rejoin
            makeInstr(Opcode::HALT)
        });
        for (uint32_t t = 0; t < 4; ++t) ctx.regs[t][0] = t;  // r0[t] = t

        uint32_t div_before = cu_test.getDivergenceEvents();
        cu_test.executeWarp(ctx);

        CHECK(ctx.state == WarpState::COMPLETE, "Warp COMPLETE");
        CHECK(ctx.regs[0][1] == 100, "Thread 0: r1=100 (fell through)");
        CHECK(ctx.regs[1][1] ==   0, "Thread 1: r1=0 (was masked)");
        CHECK(ctx.regs[2][1] ==   0, "Thread 2: r1=0 (was masked)");
        CHECK(ctx.regs[3][1] ==   0, "Thread 3: r1=0 (was masked)");
        CHECK(ctx.regs[0][2] == 100, "Thread 0: r2=100");
        CHECK(cu_test.getDivergenceEvents() == div_before + 1,
              "Divergence event counted");
    }

    // ── 6c: VFMADD vector fused multiply-add (via cu_test) ────────────────────
    // r3[t]={1,2,3,4}, r4[t]={5,6,7,8}, r5[t]=10
    // VFMADD r5, r3, r4  ->  r5[t] = r3[t]*r4[t] + r5[t]
    //   = {1*5+10, 2*6+10, 3*7+10, 4*8+10} = {15, 22, 31, 42}
    LOG_SEP("6c: VFMADD – fused multiply-add across 4 threads");
    {
        WarpContext ctx = makeContext(4, 4, {
            makeInstr(Opcode::VFMADD, 5, 3, 4, 0),  // r5 = r3*r4 + r5
            makeInstr(Opcode::HALT)
        });
        uint32_t a[] = {1,2,3,4};
        uint32_t b[] = {5,6,7,8};
        for (uint32_t t = 0; t < 4; ++t) {
            ctx.regs[t][3] = a[t];
            ctx.regs[t][4] = b[t];
            ctx.regs[t][5] = 10;
        }

        cu_test.executeWarp(ctx);

        CHECK(ctx.regs[0][5] == 15, "Thread 0: 1*5+10 = 15");
        CHECK(ctx.regs[1][5] == 22, "Thread 1: 2*6+10 = 22");
        CHECK(ctx.regs[2][5] == 31, "Thread 2: 3*7+10 = 31");
        CHECK(ctx.regs[3][5] == 42, "Thread 3: 4*8+10 = 42");
        CHECK(cu_test.getDivergenceEvents() == 1,
              "No new divergence (still 1 total from 6b)");
    }

    // ── Final statistics ──────────────────────────────────────────────────────
    LOG_SEP("Final Statistics");
    std::cout << "  top – total instructions : " << top.getTotalInstructions() << "\n"
              << "  top – L1 cache hits      : " << top.getL1CacheHits()       << "\n"
              << "  top – L1 cache misses    : " << top.getL1CacheMisses()     << "\n"
              << "  top – divergence events  : " << top.getDivergenceEvents()  << "\n"
              << "  cu_test – instructions   : " << cu_test.getTotalInstructions() << "\n"
              << "  cu_test – divergence     : " << cu_test.getDivergenceEvents()  << "\n";

    // ── Final result ──────────────────────────────────────────────────────────
    LOG_SEP("");
    if (overall_pass)
        std::cout << "[PASS] All Phase 0 – Phase 6 checks passed.\n\n";
    else
        std::cout << "[FAIL] One or more checks failed – see above.\n\n";

    Platform::printSimulationStats(top.getTotalCycles(),
                                   top.getTotalInstructions());
    return overall_pass ? 0 : 1;
}