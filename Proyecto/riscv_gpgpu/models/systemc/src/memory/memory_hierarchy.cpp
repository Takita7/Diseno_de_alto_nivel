// memory_hierarchy.cpp – Phase 0 stub. Phase 1 adds cache + memory logic.

#include "memory_hierarchy.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

MemoryHierarchy::MemoryHierarchy(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config)
{
    shared_memory_.resize(config_.shared_mem_size, 0);
    LOG_INFO("MemoryHierarchy initialized: L1="
             + std::to_string(config_.l1_cache_size   / 1024) + "KB  L2="
             + std::to_string(config_.l2_cache_size   / 1024) + "KB  shared="
             + std::to_string(config_.shared_mem_size / 1024) + "KB (stub)");
}

MemoryHierarchy::~MemoryHierarchy() {}

bool MemoryHierarchy::loadWord(Address, uint32_t& data, uint32_t& latency) {
    data    = 0;
    latency = 1;
    return true;
}

bool MemoryHierarchy::storeWord(Address, uint32_t, uint32_t& latency) {
    latency = 1;
    return true;
}

bool MemoryHierarchy::loadSharedMemory(Address, uint32_t& data) {
    data = 0;
    return true;
}

bool MemoryHierarchy::storeSharedMemory(Address, uint32_t) {
    return true;
}

bool     MemoryHierarchy::cacheHit(Address, CacheStatus& status) {
    status = CacheStatus::MISS;
    return false;
}

void     MemoryHierarchy::invalidateCache() {}

Address  MemoryHierarchy::alignAddress(Address addr)          { return addr & ~0x3ULL; }
bool     MemoryHierarchy::isSharedMemoryAddress(Address addr) const {
    return addr < config_.shared_mem_size;
}
uint32_t MemoryHierarchy::calculateLatency(CacheStatus status) {
    switch (status) {
        case CacheStatus::L1_HIT: return 1;
        case CacheStatus::L2_HIT: return 10;
        case CacheStatus::MISS:   return 100;
    }
    return 100;
}

}  // namespace riscv_gpgpu
