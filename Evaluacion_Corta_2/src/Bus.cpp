#pragma once
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <iostream>

SC_MODULE(Bus) {
    // Targets - reciben transacciones entrantes
    tlm_utils::simple_target_socket<Bus> cpu_socket;
    tlm_utils::simple_target_socket<Bus> accel_in_socket;

    // Initiators - envian transacciones salientes
    tlm_utils::simple_initiator_socket<Bus> ram_socket;
    tlm_utils::simple_initiator_socket<Bus> accel_out_socket;

    SC_CTOR(Bus)
        : cpu_socket("cpu_socket"),
          accel_in_socket("accel_in_socket"),
          ram_socket("ram_socket"),
          accel_out_socket("acell_out_socket")
    {
        cpu_socket.register_b_transport (this, &Bus::b_transport_cpu);
        accel_in_socket.register_b_transport (this, &Bus::b_transport_accel);
    }

private:
    void route(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        uint64_t addr = trans.get_addres();

        if (addr <= 0x03FFFFFF) {
            ram_socket -> b_transport(trans, delay);

        } else if (addr >= 0x10000000 && addr <= 0x1000001F) {
            trans.set_address(addr - 0x10000000);
            accel_out_socket -> b_transport(trans, delay);
            trans.set_address(addr); // restaurar direccion original

        } else {
            std::cerr << "[BUS] Direccion desconocida: 0x"
                      << std::hex << addr << std::dec << "\n";
            trans.set_reponse_status(tlm::TLN_ADDRESS_ERROR_RESPONSE);
        }

    }

    void b_transport_cpu(tlm::tlm_generic_payload& trans, sc_coreL::sc_time& delay) {
        route(trans, delay);
    }

    void b_transport_accel(tlm::tlm_generic_payload& trans, sc_core::sc_time& delay) {
        route(trans, delay);
    }
};
