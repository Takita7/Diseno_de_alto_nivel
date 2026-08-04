// memory_hierarchy.h – Memory hierarchy and cache model
//
// Phase 1: real L1/L2/global/shared memory logic via direct method calls.
// Phase 4: TLM socket will be added here when compute units bind to it.
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
        uint32_t shared_mem_size = 16 * 1024;
        uint32_t global_mem_size = 0;
        uint32_t cache_line_size = 128;
        uint32_t l1_cache_size   = 32 * 1024;
        uint32_t l2_cache_size   = 512 * 1024;
    };

    sc_core::sc_in<bool> clk{"clk"};

    // Phase 4: add TLM target socket here
    // tlm_utils::simple_target_socket<MemoryHierarchy> mem_socket;

    SC_HAS_PROCESS(MemoryHierarchy);
    MemoryHierarchy(sc_core::sc_module_name name, const Config& config);
    ~MemoryHierarchy();

    static constexpr Address SHARED_MEM_BASE = 0x00400000u;

    bool loadWord        (Address addr, uint32_t& data, uint32_t& latency, BlockID block_id = 0);
    bool storeWord       (Address addr, uint32_t  data, uint32_t& latency, BlockID block_id = 0);
    bool loadSharedMemory (BlockID block_id, Address addr, uint32_t& data);
    bool storeSharedMemory(BlockID block_id, Address addr, uint32_t  data);
    bool allocateSharedMemory(BlockID block_id, uint32_t size_bytes);
    void releaseSharedMemory(BlockID block_id);
    bool hasSharedMemory(BlockID block_id) const;

    // Bulk byte-granular access — maps 1:1 to AXI DMA transfers on FPGA.
    // writeBytes: load ELF segments and H2D buffer copies into simulation memory.
    // readBytes:  D2H result copies back to the driver/host.
    void writeBytes(Address addr, const uint8_t* data, size_t len, BlockID block_id = 0);
    void readBytes (Address addr, uint8_t*       data, size_t len, BlockID block_id = 0);

    bool cacheHit      (Address addr, CacheStatus& status);
    void invalidateCache();

    uint64_t getL1CacheHits()   const { return l1_hits_;   }
    uint64_t getL1CacheMisses() const { return l1_misses_; }
    uint64_t getL2CacheHits()   const { return l2_hits_;   }
    uint64_t getL2CacheMisses() const { return l2_misses_; }

private:
    Config config_;

    std::map<BlockID, std::vector<uint8_t>> shared_memory_;
    std::map<Address, uint32_t>   global_memory_;
    std::map<Address, uint32_t>   l1_cache_;
    std::map<Address, uint32_t>   l2_cache_;
    std::map<Address, CycleCount> cache_timestamps_;

    uint64_t l1_hits_   = 0;
    uint64_t l1_misses_ = 0;
    uint64_t l2_hits_   = 0;
    uint64_t l2_misses_ = 0;

    Address  alignAddress          (Address addr);
    bool     isSharedMemoryAddress (Address addr) const;
    uint32_t calculateLatency      (CacheStatus status);
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_MEMORY_HIERARCHY_H