#pragma once
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <vector>
#include <cstring>
#include <iostream>
#include <storage.h>

static constexpr uint32_t RAM_SIZE = 64u * 1024u* *1024u; // 64 MB

SC_MODULE(RAM){

    tlm_utils::simple_target_socket<RAM> socket;

    SC_CTOR(RAM) : socket("socket"), mem_(RAM_SIZE, 0x00) {
        socket.register_b_transport (this, &RAM::b_transport);
        socket.register_transport_dbg(this, &RAM::transport_dbg);
    }

private:
    std::vector<uint8_t> mem_;

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        tlm::tlm_command cmd = trans.get_command();
        uint64_t addr = trans.get_address();
        uint8_t* data = trans.get_data_ptr();
        unsigned int len = trans.get_data_length();

        if (addr + len > RAM_SIZE) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }
        
        if (cmd == tlm::TLM_WRITE_COMMAND)
            std::memcpy(&mem_[addr], data, len);
        else
            std::memcpy(data, &mem_[addr], len);

        delay += sc_core::sc_time(len*1.0, sc_core::SC_NS);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // Igual que b_transport pero sin modificar el tiempo de simulacion
    // Lo usa el simulator internamente para inspección/debug
    unsigned int transport_dbg(tlm::tlm_generic_payload& trans) {
        uint64_t addr = trans.get_address();
        uint8_t* data = trans.get_data_ptr();
        unsigned int len = trans.get_data_length();

        if (addr + len > RAM_SIZE) return 0;

        if (trans.get_command() == tlm::TLM_WRITE_COMMAND)
            std::memcpy(&mem_[addr], data, len);
        else
            std::memcpy(data, &mem_[addr], len);

        return len;
    }
};
