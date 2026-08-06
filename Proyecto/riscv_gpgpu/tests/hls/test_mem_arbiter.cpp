// test_mem_arbiter.cpp - regression tests for MemArbiter
//
// Design and justification: docs/hls/interfaces.md SS10.9 - memory_pipeline
// itself needs no changes (confirmed there by reading its real
// implementation), this is pure N:1 request / 1:N response plumbing
// outside both existing blocks. Formalizes the smoke test written during
// SS10.9's implementation pass (SS10.12) into real GTest coverage.
//
// Honest limitation, stated in the tests below rather than glossed over:
// at NUM_CUS=1 (the only configured value, matching every other synthesized
// config in this project so far), there is only one request source, so
// true round-robin fairness among multiple CUs is NOT exercised here -
// only the single-source forwarding/routing path is.

#include <gtest/gtest.h>

#include "scheduler/mem_arbiter.h"

using namespace riscv_gpgpu_hls;

TEST(MemArbiter, ArbitrateOnceIsANoOpWhenNothingIsPending) {
    MemArbiterFlat arb;
    hls::stream<mem_req_t> req_in[NUM_CUS];
    hls::stream<mem_req_t> mem_req_out;

    EXPECT_FALSE(arb.arbitrateOnce(req_in, mem_req_out))
        << "must not block/hang when every input stream is empty";
    EXPECT_TRUE(mem_req_out.empty());
}

TEST(MemArbiter, RouteResponseIsANoOpWhenNothingIsPending) {
    MemArbiterFlat arb;
    hls::stream<mem_resp_t> mem_resp_in;
    hls::stream<mem_resp_t> resp_out[NUM_CUS];

    EXPECT_FALSE(arb.routeResponse(mem_resp_in, resp_out));
}

TEST(MemArbiter, RequestForwardedUntouched) {
    MemArbiterFlat arb;
    hls::stream<mem_req_t> req_in[NUM_CUS];
    hls::stream<mem_req_t> mem_req_out;

    mem_req_t req;
    req.cu_id = 0; req.warp_id = 7; req.lane_id = 3; req.address = 0x1000;
    req.is_write = true; req.write_data = 0xCAFEBABE;
    req_in[0].write(req);

    ASSERT_TRUE(arb.arbitrateOnce(req_in, mem_req_out));
    ASSERT_FALSE(mem_req_out.empty());
    mem_req_t fwd = mem_req_out.read();

    EXPECT_EQ(fwd.cu_id, 0);
    EXPECT_EQ(fwd.warp_id, 7);
    EXPECT_EQ(fwd.lane_id, 3);
    EXPECT_EQ(fwd.address, addr_t(0x1000));
    EXPECT_TRUE(fwd.is_write);
    EXPECT_EQ(fwd.write_data, reg_t(0xCAFEBABE));
    EXPECT_TRUE(req_in[0].empty()) << "source stream must be drained after forwarding";
}

TEST(MemArbiter, ResponseRoutedByCuId) {
    MemArbiterFlat arb;
    hls::stream<mem_resp_t> mem_resp_in;
    hls::stream<mem_resp_t> resp_out[NUM_CUS];

    mem_resp_t resp;
    resp.cu_id = 0; resp.warp_id = 7; resp.lane_id = 3; resp.data = 0x12345678;
    mem_resp_in.write(resp);

    ASSERT_TRUE(arb.routeResponse(mem_resp_in, resp_out));
    ASSERT_FALSE(resp_out[0].empty());
    mem_resp_t got = resp_out[0].read();

    EXPECT_EQ(got.warp_id, 7);
    EXPECT_EQ(got.lane_id, 3);
    EXPECT_EQ(got.data, reg_t(0x12345678));
}
