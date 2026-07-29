// =============================================================================
// top_test_DPI.cpp  -  Sistema Completo Phase 5: SystemC + RTL Verilog
//
// Topologia:
//   CPU.socket       ->  Bus.cpu_socket
//   Bus.ram_socket   ->  RAM_RTL_SC.socket
//                            |
//                        Vram_axi4 (modelo C++ generado por Verilator)
//                            |
//                        ram_axi4.sv (RTL original)
//   Bus.accel_out_socket ->  Accel.cfg_socket
//   Accel.mem_socket     ->  Bus.accel_in_socket
//
// Cambio respecto a Phase 2:
//   ANTES: TLM_to_AXI4_Bridge + sc_fifos + RAM_AXI4_Model
//   AHORA: RAM_RTL_SC (wrapper SystemC sobre Vram_axi4)
//
// Bus, CPU y Accelerator NO cambiaron 
// La verificacion es identica — output.raw debe ser bit-exacto
// con el golden de Phase 1.
// =============================================================================
#include <systemc.h>
#include <tlm_utils/simple_target_socket.h>
#include "storage.h"
#include "ram_rtl_sc.h"    
#include "bus.h"
#include "cpu.h"
#include "accelerator.h"

#include <cassert>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>

#define CLR_OK   "\033[32m"
#define CLR_ERR  "\033[31m"
#define CLR_INFO "\033[36m"
#define CLR_RST  "\033[0m"

// =============================================================================
// Verificacion basada solo en archivo 
// =============================================================================
static bool verify_output(PersistentStorage& storage)
{
    std::cout << CLR_INFO << "\n[VERIF] Verificando imagen de salida..." << CLR_RST << "\n";

    std::vector<uint8_t> output = storage.load_image("images/output.raw");
    const size_t sz = output.size();
    std::cout << "  Tamanio archivo: " << sz
              << " bytes (esperado " << ImageConfig::GRAY_SIZE << ")\n";
    if (sz != ImageConfig::GRAY_SIZE) {
        std::cout << CLR_ERR << "  [FAIL] Tamanio incorrecto\n" << CLR_RST;
        return false;
    }

    bool all_zero = std::all_of(output.begin(), output.end(),
                                [](uint8_t b){ return b == 0x00; });
    if (all_zero) {
        std::cout << CLR_ERR << "  [FAIL] Salida completamente en cero\n" << CLR_RST;
        return false;
    }

    bool all_ff = std::all_of(output.begin(), output.end(),
                              [](uint8_t b){ return b == 0xFF; });
    if (all_ff) {
        std::cout << CLR_ERR << "  [FAIL] Salida completamente saturada\n" << CLR_RST;
        return false;
    }

    const uint8_t* tail = output.data() + sz - 4;
    std::cout << "  Primeros 4 bytes: ("
              << (int)output[0] << ", " << (int)output[1] << ", "
              << (int)output[2] << ", " << (int)output[3] << ")\n";
    std::cout << "  Ultimos  4 bytes: ("
              << (int)tail[0] << ", " << (int)tail[1] << ", "
              << (int)tail[2] << ", " << (int)tail[3] << ")\n";

    double mean = static_cast<double>(
        std::accumulate(output.begin(), output.end(), 0ULL)) / sz;
    std::cout << "  Luminancia media: " << mean << "\n";
    if (mean < 10.0 || mean > 245.0) {
        std::cout << CLR_ERR << "  [FAIL] Luminancia fuera de rango\n" << CLR_RST;
        return false;
    }

    std::cout << CLR_OK << "  [OK] Imagen grayscale verificada\n" << CLR_RST;
    return true;
}

// =============================================================================
// sc_main
// =============================================================================
int sc_main(int /*argc*/, char* /*argv*/[]) {

    std::cout << CLR_INFO
              << "\n=== TLM Image Processor — Phase 5 (RTL Verilog) ==="
              << CLR_RST << "\n";

    // ---- Instanciar modulos -------------------------------------------------
    PersistentStorage storage  ("storage");
    Bus               bus      ("bus");
    Accelerator       accel    ("accelerator");
    CPU               cpu      ("cpu");
    RAM_RTL_SC        ram      ("ram");   // <- reemplaza TLM_to_AXI4_Bridge
                                          //    + RAM_AXI4_Model de Phase 2

    cpu.set_storage(&storage);

    // ---- Conectar sockets ---------------------------------------------------
    // Exactamente iguales a Phase 1 — una sola linea para la RAM
    cpu.socket           .bind(bus.cpu_socket);
    bus.ram_socket       .bind(ram.socket);        // <- conexion directa, sin fifos
    bus.accel_out_socket .bind(accel.cfg_socket);
    accel.mem_socket     .bind(bus.accel_in_socket);

    // ---- Ejecutar simulacion ------------------------------------------------
    sc_core::sc_start();

    // ---- Verificacion -------------------------------------------------------
    const bool ok = verify_output(storage);

    std::cout << CLR_INFO
              << "\n=== Phase 5 completo, t=" << sc_core::sc_time_stamp()
              << " ===" << CLR_RST << "\n\n";

    return ok ? 0 : 1;
}