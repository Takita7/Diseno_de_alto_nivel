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

    // Public interface
    bool loadWord(Address addr, uint32_t& data, uint32_t& latency);
    bool storeWord(Address addr, uint32_t data, uint32_t& latency);
    bool loadSharedMemory(Address addr, uint32_t& data);
    bool storeSharedMemory(Address addr, uint32_t data);
    
    // Cache interface
    bool cacheHit(Address addr, CacheStatus& status);
    void invalidateCache();
    
    // Statistics
    uint64_t getL1CacheHits() const { return l1_hits_; }
    uint64_t getL1CacheMisses() const { return l1_misses_; }
    uint64_t getL2CacheHits() const { return l2_hits_; }
    uint64_t getL2CacheMisses() const { return l2_misses_; }

private:
    Config config_;
    
    // Memory storage
    std::vector<uint8_t> shared_memory_;
    std::map<Address, uint32_t> global_memory_;
    
    // Cache structures
    std::map<Address, uint32_t> l1_cache_;
    std::map<Address, uint32_t> l2_cache_;
    std::map<Address, CycleCount> cache_timestamps_;
    
    // Statistics
    uint64_t l1_hits_;
    uint64_t l1_misses_;
    uint64_t l2_hits_;
    uint64_t l2_misses_;
    
    // Helper methods
    Address alignAddress(Address addr);
    bool isSharedMemoryAddress(Address addr) const;
    uint32_t calculateLatency(CacheStatus status);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_MEMORY_HIERARCHY_H
