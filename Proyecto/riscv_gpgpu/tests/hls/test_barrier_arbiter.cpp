// test_barrier_arbiter.cpp - regression tests for BarrierArbiter
//
// Golden reference: GPGPUTop::simulationProcess()'s barrier_queue mechanics
// (models/systemc/src/top/top.cpp), specifically the release condition
// `barrier_queue.size() == total_warps_` (top.cpp:157), ported globally per
// docs/hls/interfaces.md SS10.6 (the barrier-scope reversal). Design per
// SS2.5.5/SS10.7 - formalizes the smoke test written during that section's
// implementation pass (SS10.12) into real GTest coverage. Deliberately
// built against real CuDispatchUnit instances, not mocks, since
// BarrierArbiter's only real job is counting events real dispatch/result
// cycles produce (SS10.7: the two classes are decoupled but meant to be
// driven together).

#include <gtest/gtest.h>

#include "scheduler/barrier_arbiter.h"
#include "scheduler/cu_dispatch_unit.h"

using namespace riscv_gpgpu_hls;

namespace {

// Mirrors the intended top-level pairing (docs/hls/interfaces.md SS10.7):
// every recordResult() on a CU is paired with an onEvent() on the arbiter.
void dispatchAndRecord(CuDispatchUnit& cu, BarrierArbiter& arb, slot_id_t slot,
                        WarpStatusCode code, barrier_id_t bid, ap_uint<16> resume_pc = 0) {
    warp_status_t st;
    st.code = code;
    st.barrier_id = bid;
    st.resume_pc = resume_pc;
    cu.recordResult(slot, st);
    arb.onEvent(code);
}

}  // namespace

TEST(BarrierArbiter, OverCapacityLaunchSetsFault) {
    BarrierArbiter arb;
    arb.launch(warp_id_t(NUM_CUS * MAX_WARPS_PER_CU + 1));
    EXPECT_TRUE(arb.launchFault());
}

TEST(BarrierArbiter, AtCapacityLaunchDoesNotFault) {
    BarrierArbiter arb;
    arb.launch(warp_id_t(NUM_CUS * MAX_WARPS_PER_CU));
    EXPECT_FALSE(arb.launchFault());
}

TEST(BarrierArbiter, ReleaseFiresOnlyOnceEveryWarpHasArrived) {
    CuDispatchUnit cu;
    BarrierArbiter arb;
    cu.launch(0, 2);
    arb.launch(2);

    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    EXPECT_FALSE(arb.releaseReady()) << "only 1 of 2 warps has arrived";

    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    EXPECT_TRUE(arb.releaseReady()) << "both warps now stalled at the same barrier";
}

TEST(BarrierArbiter, AcknowledgeReleaseResetsArrivalCounterForNextWave) {
    CuDispatchUnit cu;
    BarrierArbiter arb;
    cu.launch(0, 2);
    arb.launch(2);
    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    ASSERT_TRUE(arb.releaseReady());

    cu.releaseBarrier();
    arb.acknowledgeRelease();

    EXPECT_EQ(cu.stateOf(0), WarpSlot::State::READY);
    EXPECT_EQ(cu.stateOf(1), WarpSlot::State::READY);
    EXPECT_FALSE(arb.releaseReady()) << "stalled_count_ must reset after acknowledgeRelease()";
}

TEST(BarrierArbiter, KernelCompleteOnceEveryWarpFinishes) {
    CuDispatchUnit cu;
    BarrierArbiter arb;
    cu.launch(0, 2);
    arb.launch(2);
    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    cu.releaseBarrier();
    arb.acknowledgeRelease();

    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::COMPLETE, 0);
    EXPECT_FALSE(arb.kernelComplete());
    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::COMPLETE, 0);
    EXPECT_TRUE(arb.kernelComplete());
}

// Inherited golden-model assumption (docs/hls/interfaces.md SS10.7, not a
// new constraint introduced here): a kernel is only valid if every warp
// converges on the same barrier, or none do. If even one warp completes
// without ever hitting the barrier, stalled_count_ can never reach
// total_warps_ again - the release condition correctly never fires, the
// same way simulationProcess() would hang in software for the same input.
TEST(BarrierArbiter, NonUniformBarrierParticipationNeverReleases) {
    CuDispatchUnit cu;
    BarrierArbiter arb;
    cu.launch(0, 3);
    arb.launch(3);

    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::COMPLETE, 0);            // warp0: no barrier
    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 3, 1); // warp1
    dispatchAndRecord(cu, arb, cu.nextReadySlot(), WarpStatusCode::STALLED_AT_BARRIER, 3, 1); // warp2

    ASSERT_EQ(cu.nextReadySlot(), CuDispatchUnit::INVALID_SLOT) << "every slot dispatched once";
    EXPECT_FALSE(arb.releaseReady())
        << "release must never fire - warp0 completed without hitting the barrier";
    EXPECT_FALSE(arb.kernelComplete())
        << "kernel not complete either - warp1/2 are stalled forever";
}
