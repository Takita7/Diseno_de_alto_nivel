// test_memory_pipeline.cpp - regression tests for memory_pipeline (T023)
//
// Two levels, matching how MemoryHierarchy itself is testable in the golden
// model:
//   1. MemorySubsystem direct tests - construct it and call handleRequest()
//      directly, bypassing streams/m_axi plumbing entirely. Covers shared
//      memory, the L1->L2->global miss/fill chain, L2-hit-refills-L1, and
//      the write-through/no-write-allocate store policy.
//   2. A thin threaded smoke test of the actual memory_pipeline() top
//      function, since it is a free-running (`while(true)`) kernel -
//      standard practice for testing such a kernel is a worker thread that
//      gets detached (not joined) once the known requests are serviced,
//      since the kernel itself never returns.
//
// `global_mem` throughout is a plain heap buffer pre-filled with an "identity"
// pattern (word i holds its own byte address, i*4) so any address used has a
// known expected load value without needing a separate lookup table - a
// store test then overwrites one address with a distinguishable value to
// prove the write actually reached the backing store.

#include <gtest/gtest.h>
#include <hls_stream.h>
#include <thread>
#include <chrono>
#include <vector>
#include <cassert>

#include "memory/memory_pipeline.h"

using namespace riscv_gpgpu_hls;

namespace {

constexpr uint32_t kBufWords = 1u << 22;  // 16MB of ap_uint<32> - covers every address used below (+ a full cache line's headroom past the highest one, 0x600000)

std::vector<ap_uint<32>> makeIdentityBackedMemory() {
    std::vector<ap_uint<32>> buf(kBufWords);
    for (uint32_t i = 0; i < kBufWords; ++i) buf[i] = ap_uint<32>(i * 4);
    return buf;
}

mem_req_t makeReq(cu_id_t cu, warp_id_t warp, lane_id_t lane, addr_t addr,
                   bool is_write, reg_t write_data = 0) {
    // Catches the exact bug this test file hit twice already: an address
    // whose containing cache line runs past kBufWords, silently reading/
    // writing out of bounds (UB - manifests as garbage 0s or a crash,
    // neither of which points at the real cause). Fail loudly instead.
    assert(static_cast<uint64_t>(addr) + CACHE_LINE_BYTES < static_cast<uint64_t>(kBufWords) * 4
           && "test address + cache line exceeds makeIdentityBackedMemory()'s buffer - bump kBufWords");
    mem_req_t r;
    r.cu_id = cu; r.warp_id = warp; r.lane_id = lane;
    r.address = addr; r.is_write = is_write; r.write_data = write_data;
    return r;
}

}  // namespace

TEST(MemorySubsystem, SharedMemoryBypassesCacheAndGlobalMem) {
    MemorySubsystem mem;
    auto backing = makeIdentityBackedMemory();

    addr_t shared_addr = 0x100;  // well below SHARED_MEM_SIZE_BYTES (48KB default)
    ASSERT_LT(shared_addr, addr_t(SHARED_MEM_SIZE_BYTES));

    auto wr = mem.handleRequest(makeReq(0, 0, 0, shared_addr, true, 0xCAFEBABE), backing.data());
    (void)wr;
    auto rd = mem.handleRequest(makeReq(0, 0, 0, shared_addr, false), backing.data());
    EXPECT_EQ(static_cast<uint32_t>(rd.data), 0xCAFEBABEu);

    // Shared memory writes must never touch global_mem (golden:
    // loadSharedMemory/storeSharedMemory never reference global_memory_ at
    // all - memory_hierarchy.cpp:47-73).
    EXPECT_EQ(static_cast<uint32_t>(backing[shared_addr / 4]), shared_addr) << "shared write leaked into global_mem";
    EXPECT_EQ(mem.getL1CacheHits() + mem.getL1CacheMisses(), 0u) << "shared memory access must not touch L1/L2 counters";
}

TEST(MemorySubsystem, GlobalMissThenL1HitThenL2RefillsL1) {
    MemorySubsystem mem;
    auto backing = makeIdentityBackedMemory();

    // X, plus L1_WAYS filler addresses, all sharing X's L1 set (so the
    // fillers evict X via round-robin after exactly L1_WAYS more same-set
    // fills - the loop below, not just 2 fixed calls) but landing in
    // L1_WAYS+1 distinct L2 sets (so filling them into L1/L2 never
    // touches X's L2 entry) - lets this test force "L1 miss, L2 hit" for
    // X without a std::map-style cache that has no such distinction.
    // Recomputed (docs/hls/interfaces.md SS16.36) for L1_WAYS=4's real
    // 6-bit set index (L1_SIZE_BYTES=32KB / WAYS=4). The static_assert
    // below turns a future silent break (this test has now broken
    // silently twice - SS16.35 x2 - from unrelated config changes) into
    // a loud compile error pointing straight at this comment instead.
    static_assert(L1_WAYS == 4,
                   "fillers[] below is hand-computed for L1_WAYS=4 - recompute "
                   "(same-L1-set, distinct-L2-set addresses) if L1_WAYS or "
                   "L1_SIZE_BYTES changes again");
    addr_t X = 0x102000;
    addr_t fillers[L1_WAYS] = {0x104000, 0x106000, 0x108000, 0x10A000};
    ASSERT_GE(X, addr_t(SHARED_MEM_SIZE_BYTES));

    auto rX1 = mem.handleRequest(makeReq(0, 0, 0, X, false), backing.data());
    EXPECT_EQ(static_cast<uint32_t>(rX1.data), X);
    EXPECT_EQ(mem.getL1CacheMisses(), 1u);
    EXPECT_EQ(mem.getL2CacheMisses(), 1u);

    auto rXhit = mem.handleRequest(makeReq(0, 0, 0, X, false), backing.data());
    EXPECT_EQ(static_cast<uint32_t>(rXhit.data), X);
    EXPECT_EQ(mem.getL1CacheHits(), 1u) << "X should be an L1 hit immediately after its own fill";

    // Round-robin: X took way0; each filler takes the next way in order;
    // the L1_WAYS-th filler wraps back to way0, evicting X.
    for (int i = 0; i < L1_WAYS; ++i) {
        mem.handleRequest(makeReq(0, 0, 0, fillers[i], false), backing.data());
    }

    // X is gone from L1 (the last filler took its way) but still resident
    // in L2 (every filler used a different L2 set) - this load must be an
    // L1 miss + L2 hit, and must still return the correct data.
    auto rX2 = mem.handleRequest(makeReq(0, 0, 0, X, false), backing.data());
    EXPECT_EQ(static_cast<uint32_t>(rX2.data), X);
    EXPECT_EQ(mem.getL2CacheHits(), 1u) << "X's line should still be resident in L2";

    // The L2 hit must have refilled L1 - next access to X is an L1 hit again.
    auto rX3 = mem.handleRequest(makeReq(0, 0, 0, X, false), backing.data());
    EXPECT_EQ(static_cast<uint32_t>(rX3.data), X);
    EXPECT_EQ(mem.getL1CacheHits(), 2u) << "L2-hit fill should have made X an L1 hit again";
}

TEST(MemorySubsystem, StoreIsWriteThroughNoWriteAllocate) {
    MemorySubsystem mem;
    auto backing = makeIdentityBackedMemory();

    addr_t addr = 0x200000;
    ASSERT_GE(addr, addr_t(SHARED_MEM_SIZE_BYTES));

    // Store to an address never read/cached before - golden:
    // MemoryHierarchy::storeWord() only updates L1/L2 `if (l1_cache_.count(...))`
    // / `if (l2_cache_.count(...))`, i.e. never allocates on a store miss.
    mem.handleRequest(makeReq(0, 0, 0, addr, true, 0x12345678), backing.data());
    EXPECT_EQ(mem.getL1CacheHits() + mem.getL1CacheMisses(), 0u)
        << "store must not touch load hit/miss counters (golden storeWord doesn't either)";
    EXPECT_EQ(static_cast<uint32_t>(backing[addr / 4]), 0x12345678u)
        << "store must reach the backing store (write-through)";

    // Because the store did NOT allocate, this load is a fresh miss - but
    // must still return the just-stored value (from global_mem), not the
    // stale identity-pattern value.
    auto rd = mem.handleRequest(makeReq(0, 0, 0, addr, false), backing.data());
    EXPECT_EQ(static_cast<uint32_t>(rd.data), 0x12345678u);
    EXPECT_EQ(mem.getL1CacheMisses(), 1u) << "no-write-allocate: the store must not have pre-populated L1";
    EXPECT_EQ(mem.getL2CacheMisses(), 1u) << "no-write-allocate: the store must not have pre-populated L2";

    // Now that the load cached it, a subsequent store SHOULD update the
    // now-resident L1/L2 entry too (golden: `if (l1_cache_.count(aligned))
    // l1_cache_[aligned] = data;`).
    mem.handleRequest(makeReq(0, 0, 0, addr, true, 0x99999999), backing.data());
    auto rd2 = mem.handleRequest(makeReq(0, 0, 0, addr, false), backing.data());
    EXPECT_EQ(static_cast<uint32_t>(rd2.data), 0x99999999u);
    EXPECT_EQ(mem.getL1CacheHits(), 1u) << "L1 entry should have been updated in place, still a hit";
}

// docs/hls/interfaces.md SS7's new (vs. the on-chip-only draft) parity
// dimension: m_axi transaction counts. Both counts are fully derived from
// existing state (read bursts == l2_misses_, write bursts == store count),
// so this test is really pinning down that derivation, not independent
// behavior - see memory_pipeline.h's getGlobalMemReadBursts()/
// getGlobalMemWriteBursts() comment.
TEST(MemorySubsystem, GlobalMemTransactionCounts) {
    MemorySubsystem mem;
    auto backing = makeIdentityBackedMemory();

    addr_t a = 0x400000, b = 0x500000, c = 0x600000;  // 3 distinct lines, all cold
    mem.handleRequest(makeReq(0, 0, 0, a, false), backing.data());  // L2 miss #1 -> 1 read burst
    mem.handleRequest(makeReq(0, 0, 0, b, false), backing.data());  // L2 miss #2 -> 1 read burst
    mem.handleRequest(makeReq(0, 0, 0, a, false), backing.data());  // L1 hit -> 0 more bursts
    EXPECT_EQ(mem.getGlobalMemReadBursts(), 2u);
    EXPECT_EQ(mem.getGlobalMemReadBursts(), mem.getL2CacheMisses());

    mem.handleRequest(makeReq(0, 0, 0, c, true, 0xAAAA5555), backing.data());  // store -> 1 write burst
    mem.handleRequest(makeReq(0, 0, 0, a, true, 0x11112222), backing.data());  // store (already L1-resident) -> still 1 write burst
    EXPECT_EQ(mem.getGlobalMemWriteBursts(), 2u);
}

// Thin end-to-end smoke test of the actual top-level memory_pipeline()
// function - it's a free-running (`while(true)`) kernel that never returns,
// matching a real always-on memory subsystem (see memory_pipeline.h's file
// header), so the standard way to test it is a worker thread that services
// exactly the known requests and is then detached (not joined - the kernel
// thread is permanently blocked on the next req_in.read() after that, which
// is expected and fine for a short-lived test process).
TEST(MemoryPipelineTopFunction, StreamRoundTrip) {
    static std::vector<ap_uint<32>> backing = makeIdentityBackedMemory();

    hls::stream<mem_req_t>  req_in("req_in");
    hls::stream<mem_resp_t> resp_out("resp_out");

    std::thread kernel_thread([&]() {
        memory_pipeline(req_in, resp_out, backing.data());
    });

    addr_t addr = 0x300000;
    req_in.write(makeReq(0, 0, 0, addr, false));
    mem_resp_t resp = resp_out.read();
    EXPECT_EQ(static_cast<uint32_t>(resp.data), addr);

    req_in.write(makeReq(0, 0, 0, addr, true, 0xABCDEF01));
    resp_out.read();  // store ack, data don't-care
    req_in.write(makeReq(0, 0, 0, addr, false));
    resp = resp_out.read();
    EXPECT_EQ(static_cast<uint32_t>(resp.data), 0xABCDEF01u);

    kernel_thread.detach();
}
