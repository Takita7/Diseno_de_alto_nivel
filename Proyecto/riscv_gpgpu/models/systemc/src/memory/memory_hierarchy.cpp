// memory_hierarchy.cpp - Real memory hierarchy implementation
//
// Provides byte-addressable global memory (backed by a flat map),
// simple direct-mapped L1 cache simulation, and shared memory.
//

#include "memory_hierarchy.h"
#include "../common/logging.h"

#include <cassert>
#include <cstring>
#include <sstream>

namespace riscv_gpgpu {

MemoryHierarchy::MemoryHierarchy(sc_core::sc_module_name name, const Config& config)
    : sc_core::sc_module(name),
      config_(config),
      l1_hits_(0), l1_misses_(0),
      l2_hits_(0), l2_misses_(0) {
    shared_memory_.resize(config.shared_mem_size, 0);
}

MemoryHierarchy::~MemoryHierarchy() = default;

// ─── Internal helpers ─────────────────────────────────────────────────────────

Address MemoryHierarchy::alignAddress(Address addr) {
    return addr & ~static_cast<Address>(config_.cache_line_size - 1);
}

bool MemoryHierarchy::isSharedMemoryAddress(Address addr) const {
    // Shared memory lives below 0x10000000 (driver device buffers start there)
    return addr < 0x10000000ULL && addr < config_.shared_mem_size;
}

uint32_t MemoryHierarchy::calculateLatency(CacheStatus status) {
    switch (status) {
        case CacheStatus::HIT_L1:  return 4;
        case CacheStatus::HIT_L2:  return 12;
        case CacheStatus::MISS:    return 100;
        default:                   return 1;
    }
}

// ─── Byte-level access (used by ELF loader and compute unit) ──────────────────

void MemoryHierarchy::writeBytes(Address addr, const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
        byte_memory_[addr + i] = data[i];
    }
}

void MemoryHierarchy::readBytes(Address addr, uint8_t* data, size_t size) const {
    for (size_t i = 0; i < size; ++i) {
        auto it = byte_memory_.find(addr + i);
        data[i] = (it != byte_memory_.end()) ? it->second : 0;
    }
}

uint32_t MemoryHierarchy::fetchInstruction(Address addr) {
    uint32_t instr = 0;
    readBytes(addr, reinterpret_cast<uint8_t*>(&instr), 4);
    return instr;
}

// ─── Word-level access (legacy interface) ─────────────────────────────────────

bool MemoryHierarchy::loadWord(Address addr, uint32_t& data, uint32_t& latency) {
    if (isSharedMemoryAddress(addr)) {
        return loadSharedMemory(addr, data);
    }

    // L1 cache lookup
    Address aligned = alignAddress(addr);
    auto it = l1_cache_.find(aligned);
    if (it != l1_cache_.end()) {
        l1_hits_++;
        latency = calculateLatency(CacheStatus::HIT_L1);
        readBytes(addr, reinterpret_cast<uint8_t*>(&data), 4);
        return true;
    }

    // L2 cache lookup
    it = l2_cache_.find(aligned);
    if (it != l2_cache_.end()) {
        l2_hits_++;
        l1_misses_++;
        latency = calculateLatency(CacheStatus::HIT_L2);
        l1_cache_[aligned] = it->second;  // promote to L1
        readBytes(addr, reinterpret_cast<uint8_t*>(&data), 4);
        return true;
    }

    // Global memory
    l1_misses_++;
    l2_misses_++;
    latency = calculateLatency(CacheStatus::MISS);
    readBytes(addr, reinterpret_cast<uint8_t*>(&data), 4);
    l2_cache_[aligned] = data;
    l1_cache_[aligned] = data;
    return true;
}

bool MemoryHierarchy::storeWord(Address addr, uint32_t data, uint32_t& latency) {
    if (isSharedMemoryAddress(addr)) {
        return storeSharedMemory(addr, data);
    }
    latency = calculateLatency(CacheStatus::MISS);
    writeBytes(addr, reinterpret_cast<const uint8_t*>(&data), 4);
    // Write-through: invalidate cache lines
    Address aligned = alignAddress(addr);
    l1_cache_.erase(aligned);
    l2_cache_.erase(aligned);
    return true;
}

// ─── Typed byte-width load/store for RISC-V LB/LH/LBU/LHU/SB/SH ─────────────

bool MemoryHierarchy::loadByte(Address addr, uint8_t& data) {
    auto it = byte_memory_.find(addr);
    data = (it != byte_memory_.end()) ? it->second : 0;
    return true;
}

bool MemoryHierarchy::loadHalf(Address addr, uint16_t& data) {
    uint8_t lo, hi;
    loadByte(addr, lo);
    loadByte(addr + 1, hi);
    data = static_cast<uint16_t>(lo) | (static_cast<uint16_t>(hi) << 8);
    return true;
}

bool MemoryHierarchy::storeByte(Address addr, uint8_t data) {
    byte_memory_[addr] = data;
    return true;
}

bool MemoryHierarchy::storeHalf(Address addr, uint16_t data) {
    byte_memory_[addr]     = static_cast<uint8_t>(data & 0xFF);
    byte_memory_[addr + 1] = static_cast<uint8_t>((data >> 8) & 0xFF);
    return true;
}

// ─── Shared memory (per-CU) ───────────────────────────────────────────────────

bool MemoryHierarchy::loadSharedMemory(Address addr, uint32_t& data) {
    if (addr + 3 >= shared_memory_.size()) return false;
    std::memcpy(&data, &shared_memory_[addr], 4);
    return true;
}

bool MemoryHierarchy::storeSharedMemory(Address addr, uint32_t data) {
    if (addr + 3 >= shared_memory_.size()) return false;
    std::memcpy(&shared_memory_[addr], &data, 4);
    return true;
}

// ─── Cache control ────────────────────────────────────────────────────────────

bool MemoryHierarchy::cacheHit(Address addr, CacheStatus& status) {
    Address aligned = alignAddress(addr);
    if (l1_cache_.count(aligned)) { status = CacheStatus::HIT_L1; return true; }
    if (l2_cache_.count(aligned)) { status = CacheStatus::HIT_L2; return true; }
    status = CacheStatus::MISS;
    return false;
}

void MemoryHierarchy::invalidateCache() {
    l1_cache_.clear();
    l2_cache_.clear();
}

}  // namespace riscv_gpgpu
