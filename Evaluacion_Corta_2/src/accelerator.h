#pragma once

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <cstdint>
#include <vector>

SC_MODULE(Accelerator)
{
    // CPU -> Bus -> Accelerator config registers
    tlm_utils::simple_target_socket<Accelerator> cfg_socket;

    // Accelerator -> Bus -> RAM
    tlm_utils::simple_initiator_socket<Accelerator> mem_socket;

    SC_HAS_PROCESS(Accelerator);

    Accelerator(sc_core::sc_module_name name);

private:

    // Register offsets, relative to MemoryMap::ACCEL_BASE
    static constexpr uint32_t REG_INPUT_ADDR  = 0x00;
    static constexpr uint32_t REG_OUTPUT_ADDR = 0x04;
    static constexpr uint32_t REG_NUM_PIXELS  = 0x08;
    static constexpr uint32_t REG_CONTROL     = 0x0C;
    static constexpr uint32_t REG_STATUS      = 0x10;

    static constexpr uint32_t CONTROL_START = 1;

    enum Status : uint32_t {
        STATUS_IDLE = 0,
        STATUS_BUSY = 1,
        STATUS_DONE = 2,
        STATUS_ERROR = 3
    };

    uint32_t input_addr_;
    uint32_t output_addr_;
    uint32_t num_pixels_;
    uint32_t control_;
    uint32_t status_;

    sc_core::sc_event start_event_;

    void b_transport(tlm::tlm_generic_payload& trans,
                     sc_core::sc_time& delay);

    void process_thread();

    void mem_read(uint32_t addr, uint8_t* data, uint32_t len);
    void mem_write(uint32_t addr, const uint8_t* data, uint32_t len);

    uint32_t read_reg(uint32_t offset) const;
    void write_reg(uint32_t offset, uint32_t value);
};