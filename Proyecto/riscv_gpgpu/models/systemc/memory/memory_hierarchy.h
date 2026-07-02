// memory_hierarchy.h - Memory hierarchy and access model
//
// Implements cache hierarchy and memory access patterns
//

#ifndef RISCV_GPGPU_MEMORY_HIERARCHY_H
#define RISCV_GPGPU_MEMORY_HIERARCHY_H

#include <systemc>
#include <vector>
#include <map>
#include "../common/types.h"

namespace riscv_gpgpu {

// Note: CacheStatus is defined in common/types.h as:
//   enum class CacheStatus : uint8_t { HIT_L1, HIT_L2, MISS, ... }

class MemoryHierarchy : public sc_core::sc_module {
public:
    struct Config {
        uint32_t shared_mem_size;
        uint32_t global_mem_size;
        uint32_t cache_line_size;
        uint32_t l1_cache_size;
        uint32_t l2_cache_size;
    };

    // Ports
    sc_core::sc_in<bool> clk{"clk"};
    sc_core::sc_in<bool> reset{"reset"};

    MemoryHierarchy(sc_core::sc_module_name name, const Config& config);
    ~MemoryHierarchy();

    // ── Word-level interface (legacy) ─────────────────────────────────────────
    bool loadWord(Address addr, uint32_t& data, uint32_t& latency);
    bool storeWord(Address addr, uint32_t data, uint32_t& latency);
    bool loadSharedMemory(Address addr, uint32_t& data);
    bool storeSharedMemory(Address addr, uint32_t data);

    // ── Byte/half-word interface (used by compute unit for LB/LH/SB/SH) ──────
    bool loadByte(Address addr, uint8_t& data);
    bool loadHalf(Address addr, uint16_t& data);
    bool storeByte(Address addr, uint8_t data);
    bool storeHalf(Address addr, uint16_t data);

    // ── Bulk byte access (used by ELF loader and KernelBridge) ───────────────
    void writeBytes(Address addr, const uint8_t* data, size_t size);
    void readBytes(Address addr, uint8_t* data, size_t size) const;

    // ── Instruction fetch ─────────────────────────────────────────────────────
    uint32_t fetchInstruction(Address addr);

    // Cache interface
    bool cacheHit(Address addr, CacheStatus& status);
    void invalidateCache();

    // Statistics
    uint64_t getL1CacheHits()  const { return l1_hits_;  }
    uint64_t getL1CacheMisses() const { return l1_misses_; }
    uint64_t getL2CacheHits()  const { return l2_hits_;  }
    uint64_t getL2CacheMisses() const { return l2_misses_; }

private:
    Config config_;

    // Byte-addressable global memory (sparse)
    std::map<Address, uint8_t> byte_memory_;

    // Per-CU shared memory
    std::vector<uint8_t> shared_memory_;

    // Cache structures (word-line granularity)
    std::map<Address, uint32_t> l1_cache_;
    std::map<Address, uint32_t> l2_cache_;

    // Statistics
    uint64_t l1_hits_, l1_misses_;
    uint64_t l2_hits_, l2_misses_;

    // Helper methods
    Address alignAddress(Address addr);
    bool isSharedMemoryAddress(Address addr) const;
    uint32_t calculateLatency(CacheStatus status);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_MEMORY_HIERARCHY_H
