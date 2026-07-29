// memory_pipeline.h - HLS-synthesizable top-level kernel (T023)
//
// Golden reference: models/systemc/src/memory/memory_hierarchy.{h,cpp}
// (MemoryHierarchy::loadWord()/storeWord(), write-through/no-write-allocate
// policy, L1->L2->global read-fill chain). Signature per
// docs/hls/interfaces.md SS3.3.
//
// MemorySubsystem is the direct port of the MemoryHierarchy class itself -
// same public shape (load/store, per-tier hit/miss getters,
// invalidateCache()) - kept in this header (not hidden in the .cpp, unlike
// compute_pipeline.cpp's file-scope-static helpers) specifically so tests can
// construct and drive it directly, bypassing the streaming/m_axi plumbing,
// the same way MemoryHierarchy itself is directly unit-testable in the
// golden model.
//
// Line-granularity note (see hls_config.h's CACHE_LINE_BYTES comment,
// repeated here because it drives every design choice in this file): the
// golden model cached individual words in an unbounded std::map - no line
// structure, no burst behavior, no capacity limit. This port uses real
// WORDS_PER_LINE-word lines so L2 misses fill via one m_axi burst. Hit/miss
// RATES will differ from the golden model even when the data returned is
// identical - functional (data) parity is the bar (docs/hls/interfaces.md
// SS7), not hit-rate parity.
//
// Persistent kernel model: unlike compute_pipeline (invoked once per warp),
// memory_pipeline is a single, long-lived kernel instance servicing a
// continuous stream of requests for the accelerator's whole lifetime -
// matches real hardware (the memory subsystem is fixed hardware, not
// re-instantiated per warp) and is why the top-level function has no
// warp_id/request-count parameter at all (docs/hls/interfaces.md SS3.3).
// Its internal loop is `while (true)`, not a bounded for-loop; testing a
// free-running kernel like this needs a thread-based harness (see
// tests/hls/test_memory_pipeline.cpp).

#ifndef RISCV_GPGPU_HLS_MEMORY_PIPELINE_H
#define RISCV_GPGPU_HLS_MEMORY_PIPELINE_H

#include <hls_stream.h>
#include "../common/hls_config.h"
#include "../common/hls_types.h"
#include "cache_bank.h"

namespace riscv_gpgpu_hls {

class MemorySubsystem {
public:
    // T024 pragmas on class-member arrays must live in a member function
    // body, not next to the member declaration - `#pragma HLS` outside
    // function scope is a real Vitis HLS csynth error (HLS 207-5507),
    // caught by tests/fpga/test_flow.tcl's T020 smoke test (this compiled
    // fine under plain g++ for csim, which doesn't parse pragma placement
    // rules at all, so the earlier csim-only tests/hls/* suite never caught
    // it - see docs/hls/interfaces.md SS8 for the "csim doesn't validate
    // pragma legality" caveat this confirms).
    MemorySubsystem() {
#pragma HLS BIND_STORAGE variable=shared_mem_ type=RAM_2P impl=BRAM
        reset();
    }

    // Golden reference: MemoryHierarchy's constructor (fresh caches, zeroed
    // shared memory) + invalidateCache() (caches cleared, shared memory left
    // alone - matches memory_hierarchy.cpp:139-144, which only clears
    // l1_cache_/l2_cache_/cache_timestamps_, never touches shared_memory_).
    void reset() {
    RESET_CUS:
        for (int c = 0; c < NUM_CUS; ++c) {
            l1_caches_[c].reset();
        RESET_SHARED_WORDS:
            for (int w = 0; w < SHARED_MEM_WORDS_PER_CU; ++w) shared_mem_[c][w] = 0;
        }
        l2_cache_.reset();
        l1_hits_ = 0; l1_misses_ = 0; l2_hits_ = 0; l2_misses_ = 0; global_writes_ = 0;
    }
    void invalidateCache() {
    INVALIDATE_CUS:
        for (int c = 0; c < NUM_CUS; ++c) l1_caches_[c].reset();
        l2_cache_.reset();
    }

    // Golden reference: MemoryHierarchy::loadWord()/storeWord()
    // (memory_hierarchy.cpp:77-127). `global_mem` is the m_axi pointer the
    // top-level memory_pipeline() function receives - passed through here so
    // MemorySubsystem stays the single place that knows the load/store
    // policy, while the m_axi interface pragma itself lives on the top
    // function's parameter (docs/hls/interfaces.md SS3.3).
    mem_resp_t handleRequest(const mem_req_t& req, ap_uint<32>* global_mem) {
        mem_resp_t resp;
        resp.cu_id   = req.cu_id;
        resp.warp_id = req.warp_id;
        resp.lane_id = req.lane_id;

        addr_t aligned = alignAddress(req.address);  // golden: alignAddress() masks low 2 bits

        if (isSharedMemoryAddress(aligned)) {
            ap_uint<32> word_idx = aligned >> 2;
            if (req.is_write) {
                shared_mem_[req.cu_id][word_idx] = req.write_data;
                resp.data = 0;
            } else {
                resp.data = shared_mem_[req.cu_id][word_idx];
            }
            return resp;
        }

        if (req.is_write) {
            storeWord(aligned, req.write_data, req.cu_id, global_mem);
            resp.data = 0;  // don't-care per the memory contract (docs/hls/interfaces.md SS2.2)
        } else {
            resp.data = loadWord(aligned, req.cu_id, global_mem);
        }
        return resp;
    }

    uint64_t getL1CacheHits()   const { return l1_hits_;   }
    uint64_t getL1CacheMisses() const { return l1_misses_; }
    uint64_t getL2CacheHits()   const { return l2_hits_;   }
    uint64_t getL2CacheMisses() const { return l2_misses_; }

    // New parity dimension vs. the golden model (docs/hls/interfaces.md SS7):
    // there was nothing to count in the on-chip-only draft, since global
    // memory wasn't a real bus. Every L2 miss issues exactly one burst read
    // (the fill in loadWord()); every store issues exactly one burst write
    // (write-through, unconditional) - so these counts are fully derived
    // from l2_misses_/store-call-count, not independently tracked state that
    // could drift from them, but are exposed directly for test clarity.
    uint64_t getGlobalMemReadBursts()  const { return l2_misses_;       }
    uint64_t getGlobalMemWriteBursts() const { return global_writes_; }

private:
    static addr_t alignAddress(addr_t addr) { return addr & ~addr_t(0x3); }
    static bool isSharedMemoryAddress(addr_t addr) { return addr < addr_t(SHARED_MEM_SIZE_BYTES); }

    // Golden reference: MemoryHierarchy::loadWord()'s L1 -> L2 -> global
    // chain (memory_hierarchy.cpp:77-110), generalized from single-word fills
    // to whole-line fills (see file header's line-granularity note).
    reg_t loadWord(addr_t aligned, cu_id_t cu_id, ap_uint<32>* global_mem) {
        L1Cache& l1 = l1_caches_[cu_id];

        auto l1r = l1.lookup(aligned);
        if (l1r.hit) { ++l1_hits_; return l1r.data; }
        ++l1_misses_;

        auto l2r = l2_cache_.lookup(aligned);
        if (l2r.hit) {
            ++l2_hits_;
            L2Cache::word_t line[WORDS_PER_LINE];
            l2_cache_.readLine(l2r.way, aligned, line);
            l1.fillLine(L1Cache::lineBaseAddr(aligned), line);
            return l2r.data;
        }
        ++l2_misses_;

        // Golden: `global_memory_.find(aligned)` (defaulting to 0 if never
        // written), then fills L2 and L1 with that one word. Ours: burst-read
        // the whole containing line from external memory over m_axi, fill L2
        // and L1 with the line (see file header's line-granularity note -
        // this is the real difference from the golden model's per-word
        // fill, needed for burst efficiency, not a functional change for any
        // word that WAS written before being read).
        addr_t      line_base     = L2Cache::lineBaseAddr(aligned);
        ap_uint<32> base_word_idx = line_base >> 2;
        L2Cache::word_t line[WORDS_PER_LINE];
    FETCH_LINE_WORDS:
        for (int i = 0; i < WORDS_PER_LINE; ++i) {
#pragma HLS PIPELINE II=1
            line[i] = global_mem[base_word_idx + i];
        }
        l2_cache_.fillLine(line_base, line);
        l1.fillLine(line_base, line);

        int widx = static_cast<int>(L1Cache::lineWordIndex(aligned));
        return line[widx];
    }

    // Golden reference: MemoryHierarchy::storeWord() (memory_hierarchy.cpp:
    // 114-127) - write-through, no-write-allocate: update L1/L2 ONLY if the
    // line is already resident there, but always write through to the
    // backing store (m_axi here, `global_memory_` map there).
    void storeWord(addr_t aligned, reg_t data, cu_id_t cu_id, ap_uint<32>* global_mem) {
        L1Cache& l1 = l1_caches_[cu_id];

        auto l1r = l1.lookup(aligned);
        if (l1r.hit) l1.writeWord(l1r.way, aligned, data);

        auto l2r = l2_cache_.lookup(aligned);
        if (l2r.hit) l2_cache_.writeWord(l2r.way, aligned, data);

        global_mem[aligned >> 2] = data;  // golden: `global_memory_[aligned] = data;` - always, unconditionally
        ++global_writes_;
    }

    // T024: BIND_STORAGE only, no ARRAY_PARTITION - unlike cache_bank.h's
    // WAYS dimension, shared memory is accessed one word at a time per
    // request (handleRequest() isn't unrolled across lanes; compute_pipeline
    // issues one mem_req_t per lane sequentially - see compute_pipeline.cpp's
    // executeMemOp() comment), so a single BRAM's ports are enough.
    reg_t   shared_mem_[NUM_CUS][SHARED_MEM_WORDS_PER_CU];

    L1Cache l1_caches_[NUM_CUS];
    L2Cache l2_cache_;

    uint64_t l1_hits_ = 0, l1_misses_ = 0;
    uint64_t l2_hits_ = 0, l2_misses_ = 0;
    uint64_t global_writes_ = 0;
};

void memory_pipeline(
    hls::stream<mem_req_t>&  req_in,
    hls::stream<mem_resp_t>& resp_out,
    ap_uint<32>*             global_mem
);

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_MEMORY_PIPELINE_H
