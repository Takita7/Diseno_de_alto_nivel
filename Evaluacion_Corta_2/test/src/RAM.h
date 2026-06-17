// =============================================================================
// RAM.h  –  Módulo de memoria RAM (64 MB)
//
// TLM 2.0 simple_target_socket. Responde a transacciones de lectura y
// escritura del CPU y del Acelerador a través del Bus.
//
// =============================================================================
#pragma once

#include <systemc.h>                              
#include <tlm.h>                                 
#include <tlm_utils/simple_target_socket.h>
#include <vector>
#include <cstring>
#include <sstream>

// Tamaño de RAM: 64 MB
static constexpr uint32_t RAM_SIZE = 64u * 1024u * 1024u;

SC_MODULE(RAM) {

    tlm_utils::simple_target_socket<RAM> socket;

    SC_CTOR(RAM) : socket("socket"), mem_(RAM_SIZE, 0x00) {
        socket.register_b_transport (this, &RAM::b_transport);
        socket.register_transport_dbg(this, &RAM::transport_dbg);
        SC_REPORT_INFO("RAM", "Módulo RAM creado (64 MB)");
    }

private:
    std::vector<uint8_t> mem_;

    // -------------------------------------------------------------------------
    // b_transport  –  transacción TLM bloqueante (la usan CPU y Acelerador)
    // -------------------------------------------------------------------------
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        tlm::tlm_command cmd  = trans.get_command();
        uint64_t         addr = trans.get_address();
        uint8_t*         data = trans.get_data_ptr();
        unsigned int     len  = trans.get_data_length();

        // Verificar que el acceso no se sale del rango de 64 MB
        if (addr + len > RAM_SIZE) {
            std::ostringstream oss;
            oss << "Acceso fuera de rango: addr=0x" << std::hex << addr
                << " len=" << std::dec << len;
            SC_REPORT_WARNING("RAM", oss.str().c_str());
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        if (cmd == tlm::TLM_WRITE_COMMAND)
            std::memcpy(&mem_[addr], data, len);   // host -> RAM
        else
            std::memcpy(data, &mem_[addr], len);   // RAM -> host

        // Delay sintético: 1 ns por byte accedido
        delay += sc_core::sc_time(static_cast<double>(len), sc_core::SC_NS);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }

    // -------------------------------------------------------------------------
    // transport_dbg  –  acceso de debug (sin modificar el tiempo de simulación)
    // Lo usa el simulador para inspección interna; no genera eventos de tiempo.
    // -------------------------------------------------------------------------
    unsigned int transport_dbg(tlm::tlm_generic_payload& trans) {
        uint64_t     addr = trans.get_address();
        uint8_t*     data = trans.get_data_ptr();
        unsigned int len  = trans.get_data_length();

        if (addr + len > RAM_SIZE) return 0;

        if (trans.get_command() == tlm::TLM_WRITE_COMMAND)
            std::memcpy(&mem_[addr], data, len);   // host -> RAM
        else
            std::memcpy(data, &mem_[addr], len);   // RAM -> host

        return len;
    }
};
