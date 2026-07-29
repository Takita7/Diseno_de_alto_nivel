// cache_bank.h - N-way set-associative cache built from parallel direct-mapped banks
//
// Golden reference: models/systemc/src/memory/memory_hierarchy.{h,cpp}'s
// l1_cache_/l2_cache_ (std::map<Address,uint32_t>, effectively fully-
// associative, unbounded, word-granularity - no line/burst structure).
//
// Per docs/hls/interfaces.md SS3.2 (team direction): associativity here comes
// from WAYS parallel BANKS, each bank individually a plain direct-mapped
// array (own tag/valid/data BRAM, indexed by set only) - not a single wide
// fully-associative/CAM structure. All WAYS banks are probed in parallel
// (independent BRAM read ports -> free parallelism, no arbitration needed for
// lookup).
//
// This is used for BOTH L1 (per-CU) and L2 (shared) by instantiating with
// different <WAYS, SETS_PER_WAY> - see hls_config.h's L1_*/L2_* constants.
// memory_pipeline (T023, not yet written) owns the actual instances and the
// L1->L2->m_axi miss/fill chain; this file only implements the per-tier
// lookup/fill/write data structure.
//
// Line-granularity note (see hls_config.h's CACHE_LINE_BYTES comment): the
// golden model cached individual words with no line structure at all. This
// cache uses real WORDS_PER_LINE-word lines so misses fill via one m_axi
// burst instead of one word at a time. Hit/miss RATES will therefore differ
// from the golden model even when data returned is identical - functional
// (data) parity is the correctness bar (docs/hls/interfaces.md SS7), not
// hit-count parity.

#ifndef RISCV_GPGPU_HLS_CACHE_BANK_H
#define RISCV_GPGPU_HLS_CACHE_BANK_H

#include <ap_int.h>
#include "../common/hls_config.h"

namespace riscv_gpgpu_hls {

// Compile-time ceil(log2(n)) for n a positive power of two (all cache sizing
// in hls_config.h is chosen to be power-of-two; non-power-of-two sizes would
// need a real ceiling implementation, not needed here).
constexpr int clog2(int n) { return (n <= 1) ? 0 : 1 + clog2(n / 2); }

template<int WAYS, int SETS_PER_WAY, int WORDS_PER_LINE_T = WORDS_PER_LINE,
         int ADDR_BITS_T = ADDR_BITS>
class SetAssocCache {
public:
    static const int LINE_OFFSET_BITS = clog2(WORDS_PER_LINE_T * 4); // byte offset within line
    static const int SET_BITS         = clog2(SETS_PER_WAY);
    static const int TAG_BITS         = ADDR_BITS_T - SET_BITS - LINE_OFFSET_BITS;
    static const int WAY_BITS         = (WAYS > 1) ? clog2(WAYS) : 1;

    typedef ap_uint<ADDR_BITS_T> addr_t;
    typedef ap_uint<32>          word_t;
    typedef ap_uint<TAG_BITS>    tag_t;
    typedef ap_uint<SET_BITS>    set_t;
    typedef ap_uint<WAY_BITS>    way_t;

    struct LookupResult {
        bool   hit  = false;
        way_t  way  = 0;
        word_t data = 0;
    };

    // T024 pragmas on class-member arrays must live in a member function
    // body, not next to the member declaration - `#pragma HLS` outside
    // function scope is a real Vitis HLS csynth error (HLS 207-5507),
    // caught by tests/fpga/test_flow.tcl's T020 smoke test (plain g++
    // csim doesn't parse pragma placement rules at all, so the earlier
    // csim-only tests/hls/* suite never caught this).
    SetAssocCache() {
#pragma HLS ARRAY_PARTITION variable=tag_  dim=1 complete
#pragma HLS BIND_STORAGE    variable=tag_  type=RAM_2P impl=BRAM
#pragma HLS ARRAY_PARTITION variable=valid_ dim=1 complete
#pragma HLS BIND_STORAGE    variable=valid_ type=RAM_2P impl=BRAM
#pragma HLS ARRAY_PARTITION variable=data_ dim=1 complete
#pragma HLS BIND_STORAGE    variable=data_ type=RAM_2P impl=BRAM
    }

    void reset() {
    RESET_WAYS:
        for (int w = 0; w < WAYS; ++w) {
#pragma HLS UNROLL
        RESET_SETS:
            for (int s = 0; s < SETS_PER_WAY; ++s) {
                valid_[w][s] = false;
            }
        }
    RESET_VICTIMS:
        for (int s = 0; s < SETS_PER_WAY; ++s) {
            next_victim_[s] = 0;
        }
    }

    // Golden reference: MemoryHierarchy::loadWord()'s l1_cache_.find()/
    // l2_cache_.find() probes.
    LookupResult lookup(addr_t addr) const {
        set_t set = setIndex(addr);
        tag_t tag = tagOf(addr);
        ap_uint<clog2(WORDS_PER_LINE_T)> widx = lineWordIndex(addr);

        LookupResult r;
    LOOKUP_WAYS:
        for (int w = 0; w < WAYS; ++w) {
#pragma HLS UNROLL
            if (valid_[w][set] && tag_[w][set] == tag) {
                r.hit  = true;
                r.way  = w;
                r.data = data_[w][set][widx];
            }
        }
        return r;
    }

    // Read a whole resident line back out (needed by memory_pipeline's T023
    // L2-hit -> L1-fill path: our cache is line-granular, so filling L1 from
    // an L2 hit needs the entire line, not just the one word `lookup()`
    // returned - golden model didn't need this, its L1 fill-on-L2-hit was
    // just the single word, `l1_cache_[aligned] = data;`, since it had no
    // line structure at all).
    void readLine(way_t way, addr_t addr, word_t out[WORDS_PER_LINE_T]) const {
        set_t set = setIndex(addr);
    READ_LINE_WORDS:
        // PIPELINE, not UNROLL (T024): unrolling this would force a full
        // ARRAY_PARTITION of data_'s WORDS_PER_LINE dimension too, on top of
        // the WAYS-dimension partition below - WAYS x SETS_PER_WAY x
        // WORDS_PER_LINE independent single-word memories, for a path (line
        // fill on miss/L2-hit) that is not the common case. PIPELINE lets it
        // take a few cycles instead, at a fraction of the BRAM cost; the
        // m_axi burst read this feeds from (memory_pipeline.h's
        // FETCH_LINE_WORDS) is already the same choice for the same reason.
        for (int i = 0; i < WORDS_PER_LINE_T; ++i) {
#pragma HLS PIPELINE II=1
            out[i] = data_[way][set][i];
        }
    }

    // Update one word of an already-resident line - golden reference:
    // MemoryHierarchy::storeWord()'s `if (l1_cache_.count(aligned))
    // l1_cache_[aligned] = data;` (write only if present, never allocates).
    void writeWord(way_t way, addr_t addr, word_t value) {
        set_t set = setIndex(addr);
        ap_uint<clog2(WORDS_PER_LINE_T)> widx = lineWordIndex(addr);
        data_[way][set][widx] = value;
    }

    // Fill a full line on miss (round-robin victim selection across the
    // WAYS banks for this set) - golden reference: MemoryHierarchy::
    // loadWord()'s `l2_cache_[aligned] = data; l1_cache_[aligned] = data;`
    // fill-on-miss, generalized from one word to one line.
    way_t fillLine(addr_t addr, const word_t line[WORDS_PER_LINE_T]) {
        set_t set    = setIndex(addr);
        tag_t tag    = tagOf(addr);
        way_t victim = next_victim_[set];

    FILL_WORDS:
        // PIPELINE, not UNROLL - see readLine()'s comment, same reasoning.
        for (int i = 0; i < WORDS_PER_LINE_T; ++i) {
#pragma HLS PIPELINE II=1
            data_[victim][set][i] = line[i];
        }
        tag_[victim][set]   = tag;
        valid_[victim][set] = true;
        next_victim_[set]   = (victim == WAYS - 1) ? way_t(0) : way_t(victim + 1);
        return victim;
    }

    static set_t setIndex(addr_t addr) {
        return addr.range(LINE_OFFSET_BITS + SET_BITS - 1, LINE_OFFSET_BITS);
    }
    static tag_t tagOf(addr_t addr) {
        return addr.range(ADDR_BITS_T - 1, LINE_OFFSET_BITS + SET_BITS);
    }
    static ap_uint<clog2(WORDS_PER_LINE_T)> lineWordIndex(addr_t addr) {
        return addr.range(LINE_OFFSET_BITS - 1, 2);   // word index within the line
    }
    static addr_t lineBaseAddr(addr_t addr) {
        return (addr >> LINE_OFFSET_BITS) << LINE_OFFSET_BITS;
    }

private:
    // T024: ARRAY_PARTITION dim=1 (the WAYS dimension) complete is not an
    // optimization here, it's what makes LOOKUP_WAYS's #pragma HLS UNROLL
    // above actually mean what SS3.2 says it means: "N parallel banks, each
    // independently portable". Without it, tag_/valid_/data_ would each
    // still be ONE array with (at most) 2 physical read ports - the unrolled
    // loop reading all WAYS entries in the same cycle would have nowhere
    // near enough ports, and would not be the parallel-bank design this was
    // meant to be. Partitioning dim=1 complete gives each way its own
    // independent array (own BIND_STORAGE instance), matching the "N direct-
    // mapped banks" model exactly.
    tag_t  tag_  [WAYS][SETS_PER_WAY];
    bool   valid_[WAYS][SETS_PER_WAY];
    word_t data_ [WAYS][SETS_PER_WAY][WORDS_PER_LINE_T];

    // Accessed one set at a time (never unrolled across sets) - no
    // partitioning needed.
    way_t  next_victim_[SETS_PER_WAY];   // round-robin, per set (SS3.2)
};

// Convenience aliases matching hls_config.h's L1/L2 sizing.
typedef SetAssocCache<L1_WAYS, L1_SETS_PER_WAY> L1Cache;
typedef SetAssocCache<L2_WAYS, L2_SETS_PER_WAY> L2Cache;

}  // namespace riscv_gpgpu_hls

#endif  // RISCV_GPGPU_HLS_CACHE_BANK_H
