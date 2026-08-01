// test_barrier_arbiter.cpp - regression tests for global barrier arbitration
// (barrier_arbiter.h's free functions)
//
// Golden reference: GPGPUTop::simulationProcess()'s barrier_queue mechanics
// (models/systemc/src/top/top.cpp), specifically the release condition
// `barrier_queue.size() == total_warps_` (top.cpp:157), ported globally per
// docs/hls/interfaces.md SS10.6 (the barrier-scope reversal). Design per
// SS2.5.5/SS10.7/SS16.6 - same scenarios/assertions as before the SS16.6
// redesign, now driving free functions against local BarrierState/
// WarpSlot[] instead of BarrierArbiter/CuDispatchUnit instances.
// Deliberately built against the real warp-slot free functions, not mocks,
// since BarrierArbiter's only real job is counting events real dispatch/
// result cycles produce (SS10.7: decoupled but meant to be driven
// together).

#include <gtest/gtest.h>

#include "scheduler/barrier_arbiter.h"
#include "scheduler/cu_dispatch_unit.h"

using namespace riscv_gpgpu_hls;

namespace {

// Mirrors the intended top-level pairing (docs/hls/interfaces.md SS10.7):
// every recordResult() on a slot is paired with a barrierOnEvent() on the
// arbiter state.
void dispatchAndRecord(WarpSlot (&slots)[MAX_WARPS_PER_CU], BarrierState& b,
                        slot_id_t slot, WarpStatusCode code, barrier_id_t bid,
                        ap_uint<16> resume_pc = 0) {
    warp_status_t st;
    st.code = code;
    st.barrier_id = bid;
    st.resume_pc = resume_pc;
    recordResult(slots, slot, st);
    barrierOnEvent(b, code);
}

}  // namespace

TEST(BarrierArbiter, OverCapacityLaunchSetsFault) {
    BarrierState b;
    barrierLaunch(b, warp_id_t(NUM_CUS * MAX_WARPS_PER_CU + 1));
    EXPECT_TRUE(barrierLaunchFault(b));
}

TEST(BarrierArbiter, AtCapacityLaunchDoesNotFault) {
    BarrierState b;
    barrierLaunch(b, warp_id_t(NUM_CUS * MAX_WARPS_PER_CU));
    EXPECT_FALSE(barrierLaunchFault(b));
}

TEST(BarrierArbiter, ReleaseFiresOnlyOnceEveryWarpHasArrived) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    BarrierState b;
    // launchSlots()'s real assignment formula is `w = cu_id + slot*NUM_CUS`
    // - to get exactly 2 READY slots on cu_id=0 regardless of NUM_CUS's
    // real value, total_warps must be 1*NUM_CUS+1 (docs/hls/interfaces.md
    // SS16.37 - this test crashed with a real stack-smashing out-of-bounds
    // write, via nextReadySlot() returning INVALID_SLOT, before this fix).
    // barrierLaunch()'s own total_warps stays a literal 2 - it's testing
    // BarrierState's arrival-counting behavior for 2 real events, a
    // separate concern from how those 2 slots got resident on this CU.
    launchSlots(slots, cu_id_t(0), warp_id_t(1 * NUM_CUS + 1));
    barrierLaunch(b, warp_id_t(2));

    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    EXPECT_FALSE(barrierReleaseReady(b)) << "only 1 of 2 warps has arrived";

    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    EXPECT_TRUE(barrierReleaseReady(b)) << "both warps now stalled at the same barrier";
}

TEST(BarrierArbiter, AcknowledgeReleaseResetsArrivalCounterForNextWave) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    BarrierState b;
    // Same NUM_CUS-derived total_warps as the test above (SS16.37) - 2
    // READY slots on cu_id=0 regardless of NUM_CUS.
    launchSlots(slots, cu_id_t(0), warp_id_t(1 * NUM_CUS + 1));
    barrierLaunch(b, warp_id_t(2));
    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    ASSERT_TRUE(barrierReleaseReady(b));

    releaseBarrierSlots(slots);
    barrierAcknowledgeRelease(b);

    EXPECT_EQ(slots[0].state, WarpSlot::State::READY);
    EXPECT_EQ(slots[1].state, WarpSlot::State::READY);
    EXPECT_FALSE(barrierReleaseReady(b)) << "stalled_count_ must reset after acknowledgeRelease()";
}

TEST(BarrierArbiter, KernelCompleteOnceEveryWarpFinishes) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    BarrierState b;
    // Same NUM_CUS-derived total_warps as the tests above (SS16.37) - 2
    // READY slots on cu_id=0 regardless of NUM_CUS.
    launchSlots(slots, cu_id_t(0), warp_id_t(1 * NUM_CUS + 1));
    barrierLaunch(b, warp_id_t(2));
    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 9, 1);
    releaseBarrierSlots(slots);
    barrierAcknowledgeRelease(b);

    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::COMPLETE, 0);
    EXPECT_FALSE(barrierKernelComplete(b));
    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::COMPLETE, 0);
    EXPECT_TRUE(barrierKernelComplete(b));
}

// Inherited golden-model assumption (docs/hls/interfaces.md SS10.7, not a
// new constraint introduced here): a kernel is only valid if every warp
// converges on the same barrier, or none do. If even one warp completes
// without ever hitting the barrier, stalled_count_ can never reach
// total_warps_ again - the release condition correctly never fires, the
// same way simulationProcess() would hang in software for the same input.
TEST(BarrierArbiter, NonUniformBarrierParticipationNeverReleases) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    BarrierState b;
    // 3 READY slots on cu_id=0 regardless of NUM_CUS (SS16.37 - same
    // derivation as the K=2 tests above, generalized to K=3:
    // (K-1)*NUM_CUS+1).
    launchSlots(slots, cu_id_t(0), warp_id_t(2 * NUM_CUS + 1));
    barrierLaunch(b, warp_id_t(3));

    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::COMPLETE, 0);            // warp0: no barrier
    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 3, 1); // warp1
    dispatchAndRecord(slots, b, nextReadySlot(slots), WarpStatusCode::STALLED_AT_BARRIER, 3, 1); // warp2

    ASSERT_EQ(nextReadySlot(slots), INVALID_SLOT) << "every slot dispatched once";
    EXPECT_FALSE(barrierReleaseReady(b))
        << "release must never fire - warp0 completed without hitting the barrier";
    EXPECT_FALSE(barrierKernelComplete(b))
        << "kernel not complete either - warp1/2 are stalled forever";
}
