// memory_hierarchy.cpp - Memory hierarchy implementation
//

#include "memory_hierarchy.h"
#include "../common/logging.h"

namespace riscv_gpgpu {

MemoryHierarchy::MemoryHierarchy(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      l1_hits_(0),
      l1_misses_(0),
      l2_hits_(0),
      l2_misses_(0) {
    
    // Initialize memory
    shared_memory_.resize(config.shared_mem_size, 0);
    
    LOG_INFO("MemoryHierarchy initialized");
}

MemoryHierarchy::~MemoryHierarchy() {
    LOG_INFO("MemoryHierarchy destroyed");
}

bool MemoryHierarchy::loadWord(Address addr, uint32_t& data, uint32_t& latency) {
    // Check L1 cache
    Address aligned_addr = alignAddress(addr);
    
    auto l1_it = l1_cache_.find(aligned_addr);
    if (l1_it != l1_cache_.end()) {
        data = l1_it->second;
        latency = 1;  // L1 hit latency
        l1_hits_++;
        return true;
    }
    
    l1_misses_++;
    
    // Check L2 cache
    auto l2_it = l2_cache_.find(aligned_addr);
    if (l2_it != l2_cache_.end()) {
        data = l2_it->second;
        l1_cache_[aligned_addr] = data;  // Load into L1
        latency = 4;  // L2 hit latency
        l2_hits_++;
        return true;
    }
    
    l2_misses_++;
    
    // Load from global memory
    auto global_it = global_memory_.find(aligned_addr);
    if (global_it != global_memory_.end()) {
        data = global_it->second;
        l1_cache_[aligned_addr] = data;
        l2_cache_[aligned_addr] = data;
        latency = 50;  // Global memory latency
        return true;
    }
    
    latency = 0;
    return false;  // Address not found
}

bool MemoryHierarchy::storeWord(Address addr, uint32_t data, uint32_t& latency) {
    Address aligned_addr = alignAddress(addr);
    
    // Store to all levels (write-through)
    l1_cache_[aligned_addr] = data;
    l2_cache_[aligned_addr] = data;
    global_memory_[aligned_addr] = data;
    
    latency = 1;  // Store latency
    return true;
}

bool MemoryHierarchy::loadSharedMemory(Address addr, uint32_t& data) {
    if (addr + 4 > shared_memory_.size()) {
        return false;
    }
    
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= (shared_memory_[addr + i] << (i * 8));
    }
    
    data = value;
    return true;
}

bool MemoryHierarchy::storeSharedMemory(Address addr, uint32_t data) {
    if (addr + 4 > shared_memory_.size()) {
        return false;
    }
    
    for (int i = 0; i < 4; ++i) {
        shared_memory_[addr + i] = (data >> (i * 8)) & 0xFF;
    }
    
    return true;
}

bool MemoryHierarchy::cacheHit(Address addr, CacheStatus& status) {
    Address aligned_addr = alignAddress(addr);
    
    if (l1_cache_.find(aligned_addr) != l1_cache_.end()) {
        status = CacheStatus::HIT;
        return true;
    }
    
    if (l2_cache_.find(aligned_addr) != l2_cache_.end()) {
        status = CacheStatus::HIT;
        return true;
    }
    
    status = CacheStatus::MISS;
    return false;
}

void MemoryHierarchy::invalidateCache() {
    l1_cache_.clear();
    l2_cache_.clear();
}

Address MemoryHierarchy::alignAddress(Address addr) {
    return (addr / config_.cache_line_size) * config_.cache_line_size;
}

bool MemoryHierarchy::isSharedMemoryAddress(Address addr) const {
    return addr < config_.shared_mem_size;
}

uint32_t MemoryHierarchy::calculateLatency(CacheStatus status) {
    switch (status) {
        case CacheStatus::HIT:
            return 1;
        case CacheStatus::MISS:
            return 50;
        default:
            return 1;
    }
}

}  // namespace riscv_gpgpu
