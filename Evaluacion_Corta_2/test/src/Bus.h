// =============================================================================
// Bus.h  –  Router TLM entre CPU/Acelerador - RAM/Acelerador
//
// Mapa de direcciones:
//   0x00000000 – 0x03FFFFFF  →  RAM       (64 MB)
//   0x04000000 – 0x040000FF  →  Acelerador (registros de config)
//
// El Bus tiene DOS target sockets de entrada:
//   cpu_socket   – conectado al initiator del CPU
//   accel_in_socket – conectado al initiator del Acelerador (para que
//                     el Acelerador pueda leer/escribir en la RAM)
//
// Y DOS initiator sockets de salida:
//   ram_socket       – conectado al target de la RAM
//   accel_out_socket – conectado al target del Acelerador (config regs)
//
// =============================================================================
#pragma once

#include <systemc.h>                             
#include <tlm.h>                                 
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <sstream>

// Mapa de memoria (también lo usan CPU y Acelerador)
namespace MemoryMap {
    // RAM: 64 MB completos
    constexpr uint64_t RAM_BASE  = 0x00000000;
    constexpr uint64_t RAM_END   = 0x03FFFFFF;

    // Acelerador: 256 bytes para registros de configuración
    constexpr uint64_t ACCEL_BASE = 0x04000000;
    constexpr uint64_t ACCEL_END  = 0x040000FF;
}

SC_MODULE(Bus) {

    // Sockets de entrada (targets) — reciben transacciones
    tlm_utils::simple_target_socket<Bus> cpu_socket;
    // accel_in_socket se agrega cuando se integre el Acelerador

    // Sockets de salida (initiators) — reenvían transacciones
    tlm_utils::simple_initiator_socket<Bus> ram_socket;
    tlm_utils::simple_initiator_socket<Bus> accel_out_socket;

    SC_CTOR(Bus)
        : cpu_socket("cpu_socket"),
          ram_socket("ram_socket"),
          accel_out_socket("accel_out_socket")
    {
        cpu_socket.register_b_transport(this, &Bus::b_transport_cpu);
        SC_REPORT_INFO("Bus", "Módulo Bus creado");
    }

private:

    // -------------------------------------------------------------------------
    // route  –  decodifica la dirección y reenvía la transacción al destino
    // -------------------------------------------------------------------------
    void route(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {

        uint64_t addr = trans.get_address(); 

        if (addr >= MemoryMap::RAM_BASE && addr <= MemoryMap::RAM_END) {
            //RAM: la dirección ya es relativa a la base (base = 0), no hay offset
            ram_socket->b_transport(trans, delay);

        } else if (addr >= MemoryMap::ACCEL_BASE && addr <= MemoryMap::ACCEL_END) {
            //Acelerador: ajustar a offset relativo dentro del espacio de registros
            uint64_t relative = addr - MemoryMap::ACCEL_BASE;
            trans.set_address(relative);
            accel_out_socket->b_transport(trans, delay);
            trans.set_address(addr);    // restaurar dirección original

        } else {
            std::ostringstream oss;
            oss << "Dirección fuera de mapa: 0x" << std::hex << addr;
            SC_REPORT_WARNING("Bus", oss.str().c_str());
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
        }
    }

    void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        route(trans, delay);
    }
    // b_transport_accel se agrega junto con accel_in_socket
};