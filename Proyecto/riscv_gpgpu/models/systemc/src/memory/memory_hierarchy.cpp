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
    const uint64_t end = static_cast<uint64_t>(SHARED_MEM_BASE) + config_.shared_mem_size;
    return addr >= SHARED_MEM_BASE && static_cast<uint64_t>(addr) < end;
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

bool MemoryHierarchy::allocateSharedMemory(BlockID block_id, uint32_t size_bytes) {
    const uint32_t requested = size_bytes;
    if (requested > config_.shared_mem_size) {
        LOG_WARNING("MemoryHierarchy: shared allocation exceeds configured capacity for block "
                    + std::to_string(block_id));
        return false;
    }
    shared_memory_[block_id] = std::vector<uint8_t>(requested, 0);
    return true;
}

void MemoryHierarchy::releaseSharedMemory(BlockID block_id) {
    shared_memory_.erase(block_id);
}

bool MemoryHierarchy::hasSharedMemory(BlockID block_id) const {
    return shared_memory_.count(block_id) != 0;
}

bool MemoryHierarchy::loadSharedMemory(BlockID block_id, Address addr, uint32_t& data) {
    auto block = shared_memory_.find(block_id);
    if (addr < SHARED_MEM_BASE) return false;
    const uint64_t offset = static_cast<uint64_t>(addr) - SHARED_MEM_BASE;
    if (block == shared_memory_.end() || offset + 4 > block->second.size()) {
        LOG_WARNING("MemoryHierarchy: shared read out of range for block "
                    + std::to_string(block_id) + " @ " + std::to_string(addr));
        return false;
    }
    data = static_cast<uint32_t>(block->second[offset])
         | (static_cast<uint32_t>(block->second[offset + 1]) <<  8)
         | (static_cast<uint32_t>(block->second[offset + 2]) << 16)
         | (static_cast<uint32_t>(block->second[offset + 3]) << 24);
    return true;
}

bool MemoryHierarchy::storeSharedMemory(BlockID block_id, Address addr, uint32_t data) {
    auto block = shared_memory_.find(block_id);
    if (addr < SHARED_MEM_BASE) return false;
    const uint64_t offset = static_cast<uint64_t>(addr) - SHARED_MEM_BASE;
    if (block == shared_memory_.end() || offset + 4 > block->second.size()) {
        LOG_WARNING("MemoryHierarchy: shared write out of range for block "
                    + std::to_string(block_id) + " @ " + std::to_string(addr));
        return false;
    }
    block->second[offset]     =  data        & 0xFF;
    block->second[offset + 1] = (data >>  8) & 0xFF;
    block->second[offset + 2] = (data >> 16) & 0xFF;
    block->second[offset + 3] = (data >> 24) & 0xFF;
    return true;
}

// ── Read path: L1 → L2 → global ──────────────────────────────────────────────

bool MemoryHierarchy::loadWord(Address addr, uint32_t& data, uint32_t& latency, BlockID block_id) {
    Address aligned = alignAddress(addr);

    if (isSharedMemoryAddress(aligned)) {
        latency = 1;
        return loadSharedMemory(block_id, aligned, data);
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

bool MemoryHierarchy::storeWord(Address addr, uint32_t data, uint32_t& latency, BlockID block_id) {
    Address aligned = alignAddress(addr);

    if (isSharedMemoryAddress(aligned)) {
        latency = 1;
        return storeSharedMemory(block_id, aligned, data);
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

// ── Bulk byte access (AXI DMA analog) ────────────────────────────────────────
//
// These implement the same contract as AXI4 DMA bursts on the FPGA:
//   writeBytes = ARM → FPGA instruction/data memory transfer (ELF load, H2D copy)
//   readBytes  = FPGA → ARM result transfer (D2H copy)
//
// Each byte is stored via a read-modify-write on the word-granular backing store.

void MemoryHierarchy::writeBytes(Address addr, const uint8_t* data, size_t len, BlockID block_id) {
    const uint64_t shared_end = static_cast<uint64_t>(SHARED_MEM_BASE) + config_.shared_mem_size;
    const uint64_t request_end = static_cast<uint64_t>(addr) + len;
    if (addr >= SHARED_MEM_BASE && static_cast<uint64_t>(addr) < shared_end) {
        auto block = shared_memory_.find(block_id);
        const uint64_t offset = static_cast<uint64_t>(addr) - SHARED_MEM_BASE;
        if (block == shared_memory_.end() || request_end > shared_end
            || offset + len > block->second.size()) {
            LOG_WARNING("MemoryHierarchy: shared byte write out of range for block "
                        + std::to_string(block_id));
            return;
        }
        std::copy(data, data + len, block->second.begin() + offset);
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        Address byte_addr = addr + static_cast<Address>(i);
        Address word_addr = byte_addr & ~static_cast<Address>(0x3);
        uint32_t shift    = (byte_addr & 0x3u) * 8u;
        uint32_t& word    = global_memory_[word_addr];
        word = (word & ~(0xFFu << shift)) | (static_cast<uint32_t>(data[i]) << shift);
        l1_cache_.erase(word_addr);
        l2_cache_.erase(word_addr);
    }
}

void MemoryHierarchy::readBytes(Address addr, uint8_t* data, size_t len, BlockID block_id) {
    const uint64_t shared_end = static_cast<uint64_t>(SHARED_MEM_BASE) + config_.shared_mem_size;
    const uint64_t request_end = static_cast<uint64_t>(addr) + len;
    if (addr >= SHARED_MEM_BASE && static_cast<uint64_t>(addr) < shared_end) {
        auto block = shared_memory_.find(block_id);
        const uint64_t offset = static_cast<uint64_t>(addr) - SHARED_MEM_BASE;
        if (block == shared_memory_.end() || request_end > shared_end
            || offset + len > block->second.size()) {
            LOG_WARNING("MemoryHierarchy: shared byte read out of range for block "
                        + std::to_string(block_id));
            std::fill(data, data + len, 0);
            return;
        }
        std::copy(block->second.begin() + offset,
                  block->second.begin() + offset + len, data);
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        Address byte_addr = addr + static_cast<Address>(i);
        Address word_addr = byte_addr & ~static_cast<Address>(0x3);
        uint32_t shift    = (byte_addr & 0x3u) * 8u;
        auto it = global_memory_.find(word_addr);
        data[i] = (it != global_memory_.end())
                  ? static_cast<uint8_t>((it->second >> shift) & 0xFFu)
                  : 0u;
    }
}

}  // namespace riscv_gpgpu
