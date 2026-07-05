// memory_hierarchy.cpp – Phase 1: real cache + memory logic
//
// Cache policy: write-through, no-write-allocate
//   Read  path: L1 → L2 → global (fill on miss)
//   Write path: always write to global; update L1/L2 only if line present
//
// Phase 4 will add b_transport + TLM socket registration here.
//

#include "memory_hierarchy.h"
#include "../common/platform.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

MemoryHierarchy::MemoryHierarchy(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name), config_(config)
{
    shared_memory_.resize(config_.shared_mem_size, 0);
    LOG_INFO("MemoryHierarchy initialized: L1="
             + std::to_string(config_.l1_cache_size   / 1024) + "KB  L2="
             + std::to_string(config_.l2_cache_size   / 1024) + "KB  shared="
             + std::to_string(config_.shared_mem_size / 1024) + "KB");
}

MemoryHierarchy::~MemoryHierarchy() {}

// ── Helpers ───────────────────────────────────────────────────────────────────

Address MemoryHierarchy::alignAddress(Address addr) {
    return addr & ~static_cast<Address>(0x3);
}

bool MemoryHierarchy::isSharedMemoryAddress(Address addr) const {
    return addr < static_cast<Address>(config_.shared_mem_size);
}

uint32_t MemoryHierarchy::calculateLatency(CacheStatus status) {
    switch (status) {
        case CacheStatus::L1_HIT: return 1;
        case CacheStatus::L2_HIT: return 10;
        case CacheStatus::MISS:   return 100;
    }
    return 100;
}

// ── Shared memory ─────────────────────────────────────────────────────────────

bool MemoryHierarchy::loadSharedMemory(Address addr, uint32_t& data) {
    if (addr + 3 >= static_cast<Address>(config_.shared_mem_size)) {
        LOG_WARNING("MemoryHierarchy: shared read out of range @ "
                    + std::to_string(addr));
        return false;
    }
    data = static_cast<uint32_t>(shared_memory_[addr])
         | (static_cast<uint32_t>(shared_memory_[addr + 1]) <<  8)
         | (static_cast<uint32_t>(shared_memory_[addr + 2]) << 16)
         | (static_cast<uint32_t>(shared_memory_[addr + 3]) << 24);
    return true;
}

bool MemoryHierarchy::storeSharedMemory(Address addr, uint32_t data) {
    if (addr + 3 >= static_cast<Address>(config_.shared_mem_size)) {
        LOG_WARNING("MemoryHierarchy: shared write out of range @ "
                    + std::to_string(addr));
        return false;
    }
    shared_memory_[addr]     =  data        & 0xFF;
    shared_memory_[addr + 1] = (data >>  8) & 0xFF;
    shared_memory_[addr + 2] = (data >> 16) & 0xFF;
    shared_memory_[addr + 3] = (data >> 24) & 0xFF;
    return true;
}

// ── Read path: L1 → L2 → global ──────────────────────────────────────────────

bool MemoryHierarchy::loadWord(Address addr, uint32_t& data, uint32_t& latency) {
    Address aligned = alignAddress(addr);

    if (isSharedMemoryAddress(aligned)) {
        latency = 1;
        return loadSharedMemory(aligned, data);
    }

    auto it1 = l1_cache_.find(aligned);
    if (it1 != l1_cache_.end()) {
        data    = it1->second;
        latency = calculateLatency(CacheStatus::L1_HIT);
        ++l1_hits_;
        return true;
    }
    ++l1_misses_;

    auto it2 = l2_cache_.find(aligned);
    if (it2 != l2_cache_.end()) {
        data             = it2->second;
        l1_cache_[aligned] = data;
        latency          = calculateLatency(CacheStatus::L2_HIT);
        ++l2_hits_;
        return true;
    }
    ++l2_misses_;

    auto it_g = global_memory_.find(aligned);
    data = (it_g != global_memory_.end()) ? it_g->second : 0u;
    l2_cache_[aligned] = data;
    l1_cache_[aligned] = data;
    latency = calculateLatency(CacheStatus::MISS);
    return true;
}

// ── Write path: write-through, no-write-allocate ──────────────────────────────

bool MemoryHierarchy::storeWord(Address addr, uint32_t data, uint32_t& latency) {
    Address aligned = alignAddress(addr);

    if (isSharedMemoryAddress(aligned)) {
        latency = 1;
        return storeSharedMemory(aligned, data);
    }

    if (l1_cache_.count(aligned)) l1_cache_[aligned] = data;
    if (l2_cache_.count(aligned)) l2_cache_[aligned] = data;
    global_memory_[aligned] = data;
    latency = calculateLatency(CacheStatus::MISS);
    return true;
}

// ── Cache utilities ───────────────────────────────────────────────────────────

bool MemoryHierarchy::cacheHit(Address addr, CacheStatus& status) {
    Address aligned = alignAddress(addr);
    if (l1_cache_.count(aligned)) { status = CacheStatus::L1_HIT; return true; }
    if (l2_cache_.count(aligned)) { status = CacheStatus::L2_HIT; return true; }
    status = CacheStatus::MISS;
    return false;
}

void MemoryHierarchy::invalidateCache() {
    l1_cache_.clear();
    l2_cache_.clear();
    cache_timestamps_.clear();
    LOG_INFO("MemoryHierarchy: caches invalidated");
}

}  // namespace riscv_gpgpu