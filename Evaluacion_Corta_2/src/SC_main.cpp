#include <systemc>
#include <iostream>
#include <stdexcept>
#include "cpu.h"
#include "ram.h"
#include "storage.h"
#include "accelerator.h"
#include "bus.h"

int sc_main(int argc, char* argv[]) {

    std::cout << "================================== \n"
    std::cout << " MP6160 EC2 - Evaluación Corta #2  \n"
    std::cout << "  Resolución: 1080p | RAM: 64 MB   \n"
    std::cout << "================================== \n"

    try {

        // Instanciación de módulos
        CPU cpu ("CPU");
        RAM ram ("RAM");
        Storage storage ("Storage");
        Accelerator accel ("Accelerator");
        Bus bus ("Bus");

        // Configuración
        storage.set_directories("images/input", "images/output");
        cpu.set_storage(&storage);

        // Binding de sockets
        cpu.socket.bind (bus.cpu_socket);
        bus.ram_socket.bind (ram.socket);
        bus.accel_out_socket.bind (accel.target_socket);
        accel.init_socket.bind (bus.accel_in_socket);

        // Inicio de simulación
        std::cout << "\n [SC_MAIN] Iniciando simulación... \n";
        sc_core::sc_start();

        std::cout << "[SC_MAIN] Tiempo total simulad: "
                  << sc_core::sc_time_stamp() << "\n";

        } catch (const std::exception& e) {
            std::cerr << "\n[SC_MAIN] ERROR: " << e.what()<< "\n";
            return 1;
        }

    return 0;
}
