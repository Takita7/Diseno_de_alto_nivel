// test_cu_dispatch_unit.cpp - regression tests for CuDispatchUnit
//
// Golden reference: WarpScheduler::generateWarps()/selectWarp() (the real,
// exercised subset - see docs/hls/interfaces.md SS10.3 Finding A) and
// GPGPUTop::simulationProcess()'s per-CU barrier bookkeeping. Design per
// docs/hls/interfaces.md SS2.5.3/SS10.7 - formalizes the smoke test written
// during that section's implementation pass (SS10.12) into real GTest
// coverage, same scenarios, same assertions.

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
    CuDispatchUnit cu;
    // 3 warps on a single CU (NUM_CUS=1 build default) fit within
    // MAX_WARPS_PER_CU=4 - slot 3 must stay EMPTY.
    cu.launch(/*cu_id=*/0, /*total_warps=*/3);

    EXPECT_EQ(cu.stateOf(0), WarpSlot::State::READY);
    EXPECT_EQ(cu.stateOf(1), WarpSlot::State::READY);
    EXPECT_EQ(cu.stateOf(2), WarpSlot::State::READY);
    EXPECT_EQ(cu.stateOf(3), WarpSlot::State::EMPTY);
    EXPECT_EQ(cu.warpIdOf(0), 0);
    EXPECT_EQ(cu.warpIdOf(1), 1);
    EXPECT_EQ(cu.warpIdOf(2), 2);
    EXPECT_FALSE(cu.allDone());
}

TEST(CuDispatchUnit, DispatchOrderIsAscendingFifo) {
    CuDispatchUnit cu;
    cu.launch(0, 3);

    // Mirrors WarpScheduler::selectWarp()'s FIFO order: generateWarps()
    // pushes warps in increasing warp_id order, and ascending slot order
    // reproduces that pop order exactly.
    EXPECT_EQ(cu.nextReadySlot(), 0);
    cu.recordResult(0, completeStatus());
    EXPECT_EQ(cu.nextReadySlot(), 1);
    cu.recordResult(1, completeStatus());
    EXPECT_EQ(cu.nextReadySlot(), 2);
}

TEST(CuDispatchUnit, RecordResultCompleteMarksSlotDone) {
    CuDispatchUnit cu;
    cu.launch(0, 1);
    cu.recordResult(0, completeStatus());
    EXPECT_EQ(cu.stateOf(0), WarpSlot::State::DONE);
}

TEST(CuDispatchUnit, RecordResultStalledCarriesBarrierIdAndResumePc) {
    CuDispatchUnit cu;
    cu.launch(0, 1);
    cu.recordResult(0, stalledStatus(/*bid=*/7, /*resume_pc=*/4));

    EXPECT_EQ(cu.stateOf(0), WarpSlot::State::STALLED);
    EXPECT_EQ(cu.barrierIdOf(0), 7);
}

TEST(CuDispatchUnit, NoReadySlotWhenNoneAvailable) {
    CuDispatchUnit cu;
    cu.launch(0, 2);
    cu.recordResult(0, completeStatus());
    cu.recordResult(1, stalledStatus(9, 1));

    EXPECT_EQ(cu.nextReadySlot(), CuDispatchUnit::INVALID_SLOT);
}

TEST(CuDispatchUnit, ReleaseBarrierRestoresOnlyStalledSlots) {
    CuDispatchUnit cu;
    cu.launch(0, 3);
    cu.recordResult(0, completeStatus());                    // slot0 -> DONE
    cu.recordResult(1, stalledStatus(7, 4));                  // slot1 -> STALLED
    cu.recordResult(2, stalledStatus(7, 4));                  // slot2 -> STALLED

    cu.releaseBarrier();

    EXPECT_EQ(cu.stateOf(0), WarpSlot::State::DONE)  << "DONE must not be touched by release";
    EXPECT_EQ(cu.stateOf(1), WarpSlot::State::READY) << "STALLED -> READY on release";
    EXPECT_EQ(cu.stateOf(2), WarpSlot::State::READY) << "STALLED -> READY on release";
    EXPECT_EQ(cu.stateOf(3), WarpSlot::State::EMPTY) << "EMPTY must not be touched by release";
}

TEST(CuDispatchUnit, BuildDispatchUsesFullActiveMaskAndCarriesResumePc) {
    CuDispatchUnit cu;
    cu.launch(0, 2);
    cu.recordResult(0, completeStatus());                        // slot0 out of the way
    cu.recordResult(1, stalledStatus(7, /*resume_pc=*/4));
    cu.releaseBarrier();

    slot_id_t s = cu.nextReadySlot();
    ASSERT_EQ(s, 1);
    warp_dispatch_t d = cu.buildDispatch(s);

    EXPECT_EQ(d.warp_id, 1);
    EXPECT_EQ(d.active_mask_init, thread_mask_t(-1))
        << "active_mask_init is always the full mask, fresh or resumed alike "
           "(matches DivergenceStack::initializeWarp() being called on every "
           "compute_pipeline invocation)";
    EXPECT_EQ(d.resume_pc, 4) << "resume_pc must survive stall -> release -> re-dispatch";
}

TEST(CuDispatchUnit, AllDoneOnlyAfterEveryActiveSlotCompletes) {
    CuDispatchUnit cu;
    cu.launch(0, 2);
    cu.recordResult(0, completeStatus());
    EXPECT_FALSE(cu.allDone()) << "slot1 still READY";

    cu.recordResult(1, completeStatus());
    EXPECT_TRUE(cu.allDone());
}
