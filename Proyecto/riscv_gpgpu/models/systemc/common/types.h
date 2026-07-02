// types.h - Common type definitions for SystemC models
//
// Defines common types and constants used throughout
// the RISCV GPGPU SystemC implementation
//

#ifndef RISCV_GPGPU_TYPES_H
#define RISCV_GPGPU_TYPES_H

#include <cstdint>
#include <cstring>

namespace riscv_gpgpu {

// Basic types
using ThreadID = uint32_t;
using WarpID = uint16_t;
using ComputeUnitID = uint16_t;
using BlockID = uint32_t;
using GridID = uint16_t;

// Memory addresses and sizes
using Address = uint64_t;
using MemorySize = uint32_t;
using DataWord = uint32_t;

// Instruction types
using Instruction = uint32_t;
using InstructionPC = uint64_t;

// Performance counters
using CycleCount = uint64_t;
using InstructionCount = uint64_t;

// Constants
namespace constants {
    // Default thread configuration
    constexpr uint32_t DEFAULT_THREADS_PER_WARP = 32;
    constexpr uint32_t DEFAULT_MAX_WARPS_PER_CU = 16;
    constexpr uint32_t DEFAULT_SHARED_MEM_SIZE = 49152;  // 48 KB
    
    // Memory constants
    constexpr uint32_t CACHE_LINE_SIZE = 128;
    constexpr uint32_t DEFAULT_L1_CACHE_SIZE = 16384;  // 16 KB
    constexpr uint32_t DEFAULT_L2_CACHE_SIZE = 262144; // 256 KB
    
    // Execution states
    constexpr uint8_t STATE_IDLE = 0;
    constexpr uint8_t STATE_READY = 1;
    constexpr uint8_t STATE_RUNNING = 2;
    constexpr uint8_t STATE_STALLED = 3;
    constexpr uint8_t STATE_COMPLETED = 4;
}

// Warp state enumeration
enum class WarpState : uint8_t {
    IDLE = 0,
    READY = 1,
    RUNNING = 2,
    STALLED = 3,
    COMPLETED = 4
};

// Memory access type
enum class MemoryAccessType : uint8_t {
    LOAD = 0,
    STORE = 1,
    ATOMIC = 2,
    PREFETCH = 3
};

// Cache hit/miss status
enum class CacheStatus : uint8_t {
    HIT = 0,
    MISS = 1,
    PENDING = 2
};

// Instruction execution status
struct ExecutionStatus {
    bool completed;
    uint32_t latency;
    uint32_t stall_reason;
};

// Memory request structure
struct MemoryRequest {
    ThreadID thread_id;
    WarpID warp_id;
    Address address;
    MemorySize size;
    MemoryAccessType type;
    uint64_t timestamp;
};

// Memory response structure
struct MemoryResponse {
    ThreadID thread_id;
    WarpID warp_id;
    Address address;
    uint32_t latency;
    CacheStatus cache_status;
    uint64_t timestamp;
};

}  // namespace riscv_gpgpu

#endif  // RISCV_GPGPU_TYPES_H
