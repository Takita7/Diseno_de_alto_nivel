// Warp lifecycle states
enum class WarpState { IDLE, READY, RUNNING, STALLED, WAITING_MEM, COMPLETE };

// Simplified RISC-V / RVV instruction
struct Instruction {
    uint32_t pc;
    uint32_t opcode;
    uint8_t  rs1, rs2, rd;
    int32_t  imm;
    bool     is_vector;    // RVV instruction flag
    bool     is_memory;    // Load / Store
    bool     is_branch;
};

// Per-warp execution context (passed via sc_fifo between modules)
struct WarpContext {
    uint32_t  warp_id;
    uint32_t  kernel_id;
    uint32_t  block_id_x, block_id_y;
    uint32_t  pc;                          // Current program counter
    uint32_t  active_mask;                 // Bitmask: which threads are active
    WarpState state;
    // Scalar registers: [thread_index][reg_index]
    std::vector<std::vector<uint32_t>> regs;
    // Vector registers (RVV): [vreg_index] → one entry per lane
    std::vector<std::vector<uint32_t>> vregs;
};

// Memory transaction (used internally by ComputeUnit / MemoryHierarchy)
struct MemTransaction {
    uint64_t             address;
    uint32_t             size_bytes;
    bool                 is_write;
    std::vector<uint8_t> data;
    uint32_t             warp_id;
    uint32_t             thread_mask;   // Which threads participate
};