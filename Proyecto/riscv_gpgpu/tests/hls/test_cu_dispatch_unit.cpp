// test_cu_dispatch_unit.cpp - regression tests for warp-slot dispatch
// bookkeeping (cu_dispatch_unit.h's free functions)
//
// Golden reference: WarpScheduler::generateWarps()/selectWarp() (the real,
// exercised subset - see docs/hls/interfaces.md SS10.3 Finding A) and
// GPGPUTop::simulationProcess()'s per-CU barrier bookkeeping. Design per
// docs/hls/interfaces.md SS2.5.3/SS10.7/SS16.6 - same scenarios, same
// assertions as before the SS16.6 redesign, now driving free functions
// against a local WarpSlot[MAX_WARPS_PER_CU] array instead of a
// CuDispatchUnit instance's own slots_ member (CuDispatchUnit itself no
// longer holds slot state at all - see cu_dispatch_unit.h's header
// comment for why).

#include <gtest/gtest.h>

#include "scheduler/cu_dispatch_unit.h"

using namespace riscv_gpgpu_hls;

namespace {

warp_status_t completeStatus() {
    warp_status_t st;
    st.code = WarpStatusCode::COMPLETE;
    return st;
}

warp_status_t stalledStatus(barrier_id_t bid, ap_uint<16> resume_pc) {
    warp_status_t st;
    st.code       = WarpStatusCode::STALLED_AT_BARRIER;
    st.barrier_id = bid;
    st.resume_pc  = resume_pc;
    return st;
}

}  // namespace

TEST(CuDispatchUnit, LaunchAssignsWarpsRoundRobinAndLeavesUnusedSlotsEmpty) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    // assignSlot()'s real formula is `w = cu_id + slot*NUM_CUS` - for
    // cu_id=0, slot s becomes READY (holding warp_id s*NUM_CUS) once
    // total_warps > s*NUM_CUS. To get exactly 3 READY slots (0,1,2) with
    // slot 3 EMPTY regardless of NUM_CUS's real value, total_warps must be
    // (2*NUM_CUS)+1 - expressed via the real constant, not a hardcoded
    // literal, so this doesn't silently break again on the next NUM_CUS
    // change the way it did going 1->2 (docs/hls/interfaces.md SS16.37).
    launchSlots(slots, /*cu_id=*/cu_id_t(0),
                /*total_warps=*/warp_id_t(2 * NUM_CUS + 1));

    EXPECT_EQ(slots[0].state, WarpSlot::State::READY);
    EXPECT_EQ(slots[1].state, WarpSlot::State::READY);
    EXPECT_EQ(slots[2].state, WarpSlot::State::READY);
    EXPECT_EQ(slots[3].state, WarpSlot::State::EMPTY);
    EXPECT_EQ(slots[0].warp_id, warp_id_t(0 * NUM_CUS));
    EXPECT_EQ(slots[1].warp_id, warp_id_t(1 * NUM_CUS));
    EXPECT_EQ(slots[2].warp_id, warp_id_t(2 * NUM_CUS));
    EXPECT_FALSE(allSlotsDone(slots));
}

TEST(CuDispatchUnit, DispatchOrderIsAscendingFifo) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    // Same NUM_CUS-derived total_warps as the test above - 3 READY slots.
    launchSlots(slots, cu_id_t(0), warp_id_t(2 * NUM_CUS + 1));

    // Mirrors WarpScheduler::selectWarp()'s FIFO order: launchSlots()
    // assigns warps in increasing warp_id order, and ascending SLOT order
    // (not warp_id - nextReadySlot()/recordResult() below are entirely
    // slot-index-based, unaffected by NUM_CUS) reproduces that pop order
    // exactly.
    EXPECT_EQ(nextReadySlot(slots), 0);
    recordResult(slots, 0, completeStatus());
    EXPECT_EQ(nextReadySlot(slots), 1);
    recordResult(slots, 1, completeStatus());
    EXPECT_EQ(nextReadySlot(slots), 2);
}

TEST(CuDispatchUnit, RecordResultCompleteMarksSlotDone) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    launchSlots(slots, cu_id_t(0), warp_id_t(1));
    recordResult(slots, 0, completeStatus());
    EXPECT_EQ(slots[0].state, WarpSlot::State::DONE);
}

TEST(CuDispatchUnit, RecordResultStalledCarriesBarrierIdAndResumePc) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    launchSlots(slots, cu_id_t(0), warp_id_t(1));
    recordResult(slots, 0, stalledStatus(/*bid=*/7, /*resume_pc=*/4));

    EXPECT_EQ(slots[0].state, WarpSlot::State::STALLED);
    EXPECT_EQ(slots[0].barrier_id, 7);
}

TEST(CuDispatchUnit, NoReadySlotWhenNoneAvailable) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    // 2 READY slots regardless of NUM_CUS (same derivation as the tests
    // above - docs/hls/interfaces.md SS16.37).
    launchSlots(slots, cu_id_t(0), warp_id_t(1 * NUM_CUS + 1));
    recordResult(slots, 0, completeStatus());
    recordResult(slots, 1, stalledStatus(9, 1));

    EXPECT_EQ(nextReadySlot(slots), INVALID_SLOT);
}

TEST(CuDispatchUnit, ReleaseBarrierRestoresOnlyStalledSlots) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    // 3 READY slots regardless of NUM_CUS (same derivation as
    // LaunchAssignsWarpsRoundRobinAndLeavesUnusedSlotsEmpty above).
    launchSlots(slots, cu_id_t(0), warp_id_t(2 * NUM_CUS + 1));
    recordResult(slots, 0, completeStatus());                    // slot0 -> DONE
    recordResult(slots, 1, stalledStatus(7, 4));                  // slot1 -> STALLED
    recordResult(slots, 2, stalledStatus(7, 4));                  // slot2 -> STALLED

    releaseBarrierSlots(slots);

    EXPECT_EQ(slots[0].state, WarpSlot::State::DONE)  << "DONE must not be touched by release";
    EXPECT_EQ(slots[1].state, WarpSlot::State::READY) << "STALLED -> READY on release";
    EXPECT_EQ(slots[2].state, WarpSlot::State::READY) << "STALLED -> READY on release";
    EXPECT_EQ(slots[3].state, WarpSlot::State::EMPTY) << "EMPTY must not be touched by release";
}

TEST(CuDispatchUnit, BuildDispatchUsesFullActiveMaskAndCarriesResumePc) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    // 2 READY slots regardless of NUM_CUS - slot1 holds warp_id
    // 1*NUM_CUS, not the literal 1 (only true when NUM_CUS==1).
    launchSlots(slots, cu_id_t(0), warp_id_t(1 * NUM_CUS + 1));
    recordResult(slots, 0, completeStatus());                        // slot0 out of the way
    recordResult(slots, 1, stalledStatus(7, /*resume_pc=*/4));
    releaseBarrierSlots(slots);

    slot_id_t s = nextReadySlot(slots);
    ASSERT_EQ(s, 1);
    warp_dispatch_t d = buildDispatch(slots, s);

    EXPECT_EQ(d.warp_id, warp_id_t(1 * NUM_CUS));
    EXPECT_EQ(d.active_mask_init, thread_mask_t(-1))
        << "active_mask_init is always the full mask, fresh or resumed alike "
           "(matches DivergenceStack::initializeWarp() being called on every "
           "compute_pipeline invocation)";
    EXPECT_EQ(d.resume_pc, 4) << "resume_pc must survive stall -> release -> re-dispatch";
}

TEST(CuDispatchUnit, AllDoneOnlyAfterEveryActiveSlotCompletes) {
    WarpSlot slots[MAX_WARPS_PER_CU];
    // 2 READY slots regardless of NUM_CUS - without this, slot1 would
    // stay EMPTY (not READY) once NUM_CUS>1, and allSlotsDone() would
    // (correctly, given its own real "every non-EMPTY slot is DONE"
    // semantics) return true after just slot0 completes - a real failure
    // this test caught directly (docs/hls/interfaces.md SS16.37).
    launchSlots(slots, cu_id_t(0), warp_id_t(1 * NUM_CUS + 1));
    recordResult(slots, 0, completeStatus());
    EXPECT_FALSE(allSlotsDone(slots)) << "slot1 still READY";

    recordResult(slots, 1, completeStatus());
    EXPECT_TRUE(allSlotsDone(slots));
}
