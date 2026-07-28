// =============================================================================
// top_test.cpp  –  Sistema Completo Phase 2: TLM + AXI4 Behavioral
//
// Topología:
//   CPU.socket           -> Bus.cpu_socket
//   Bus.ram_socket       ->  Bridge.target_socket
//   Bridge               <->  [sc_fifo AW/W/B/AR/R]  <->  RAM_AXI4_Model
//   Bus.accel_out_socket ->  Accel.cfg_socket
//   Accel.mem_socket     ->  Bus.accel_in_socket
//
// Bus, CPU y Accelerator no cambiaron respecto a Phase 1.
// En Phase 3, RAM_AXI4_Model se reemplaza por Verilog RTL vía DPI
// sin modificar nada más del sistema.
// =============================================================================
#include <systemc.h>
#include <tlm_utils/simple_target_socket.h>
#include "storage.h"
#include "axi4_if.h"               // structs de canales AXI4
#include "tlm_to_axi4_bridge.h"    // puente TLM -> AXI4
#include "ram_axi4_model.h"        // RAM behavioral con interfaz AXI4
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
// Verificación basada solo en archivo. no depende de ningún objeto C++ interno
// =============================================================================
static bool verify_output(PersistentStorage& storage)
{
    std::cout << CLR_INFO << "\n[VERIF] Verificando imagen de salida..." << CLR_RST << "\n";

    // 1. Tamaño correcto
    std::vector<uint8_t> output = storage.load_image("images/output.raw");
    const size_t sz = output.size();
    std::cout << "  Tamaño archivo: " << sz << " bytes"
              << " (esperado " << ImageConfig::GRAY_SIZE << ")\n";
    if (sz != ImageConfig::GRAY_SIZE) {
        std::cout << CLR_ERR << "  [FAIL] Tamaño incorrecto\n" << CLR_RST;
        return false;
    }

    // 2. No debe estar completamente en cero (indicaría que el acelerador no escribió)
    bool all_zero = std::all_of(output.begin(), output.end(),
                                [](uint8_t b){ return b == 0x00; });
    if (all_zero) {
        std::cout << CLR_ERR << "  [FAIL] Salida completamente en cero\n" << CLR_RST;
        return false;
    }

    // 3. No debe estar completamente en 0xFF (imagen saturada = error de conversión)
    bool all_ff = std::all_of(output.begin(), output.end(),
                              [](uint8_t b){ return b == 0xFF; });
    if (all_ff) {
        std::cout << CLR_ERR << "  [FAIL] Salida completamente saturada (0xFF)\n" << CLR_RST;
        return false;
    }

    // 4. Mostrar muestras representativas para inspección manual
    const uint8_t* tail = output.data() + sz - 4;
    std::cout << "  Primeros 4 bytes: ("
              << (int)output[0] << ", " << (int)output[1] << ", "
              << (int)output[2] << ", " << (int)output[3] << ")\n";
    std::cout << "  Últimos  4 bytes: ("
              << (int)tail[0] << ", " << (int)tail[1] << ", "
              << (int)tail[2] << ", " << (int)tail[3] << ")\n";

    // 5. Luminancia media razonable (imagen real: entre 10 y 245)
    double mean = static_cast<double>(
        std::accumulate(output.begin(), output.end(), 0ULL)) / sz;
    std::cout << "  Luminancia media: " << mean << "\n";
    if (mean < 10.0 || mean > 245.0) {
        std::cout << CLR_ERR << "  [FAIL] Luminancia media fuera de rango\n" << CLR_RST;
        return false;
    }

    std::cout << CLR_OK << "  [OK] Imagen grayscale verificada correctamente\n" << CLR_RST;
    return true;
}

// =============================================================================
// sc_main
// =============================================================================
int sc_main(int /*argc*/, char* /*argv*/[]) {

    std::cout << CLR_INFO
              << "\n=== TLM Image Processor ==="
              << CLR_RST << "\n";

    // ── Instanciar módulos ────────────────────────────────────────────────────
    PersistentStorage    storage  ("storage");
    Bus                  bus      ("bus");
    Accelerator          accel    ("accelerator");
    CPU                  cpu      ("cpu");
    TLM_to_AXI4_Bridge   bridge   ("bridge");   // puente TLM -> AXI4
    RAM_AXI4_Model       ram      ("ram");      // RAM behavioral AXI4

    cpu.set_storage(&storage);

    // ── Canales AXI4 (sc_fifo conectan bridge <-> ram) ─────────────────────────
    // Profundidad 4: permite que el bridge aguante hasta 4 beats antes de
    // que el modelo los consuma, reduciendo context-switches innecesarios.
    sc_core::sc_fifo<AXI4_AW> aw_fifo("aw_fifo", 4);
    sc_core::sc_fifo<AXI4_W>  w_fifo ("w_fifo",  4);
    sc_core::sc_fifo<AXI4_B>  b_fifo ("b_fifo",  4);
    sc_core::sc_fifo<AXI4_AR> ar_fifo("ar_fifo", 4);
    sc_core::sc_fifo<AXI4_R>  r_fifo ("r_fifo",  4);

    // ── Conectar sockets TLM ─────────────────────────────────────────────────
    cpu.socket           .bind(bus.cpu_socket);
    bus.ram_socket       .bind(bridge.target_socket);  // Bus -> Bridge (antes -> RAM directo)
    bus.accel_out_socket .bind(accel.cfg_socket);
    accel.mem_socket     .bind(bus.accel_in_socket);

    // ── Conectar canales AXI4: bridge <-> fifos <-> ram ─────────────────────────
    bridge.aw_port.bind(aw_fifo);   ram.aw_port.bind(aw_fifo);
    bridge.w_port .bind(w_fifo);    ram.w_port .bind(w_fifo);
    bridge.b_port .bind(b_fifo);    ram.b_port .bind(b_fifo);
    bridge.ar_port.bind(ar_fifo);   ram.ar_port.bind(ar_fifo);
    bridge.r_port .bind(r_fifo);    ram.r_port .bind(r_fifo);

    // ── Ejecutar simulación ───────────────────────────────────────────────────
    sc_core::sc_start();

    // ── Verificación post-simulación ────────────
    const bool ok = verify_output(storage);

    std::cout << CLR_INFO
              << "\n=== Sistema Completo, t=" << sc_core::sc_time_stamp()
              << " ===" << CLR_RST << "\n\n";

    return ok ? 0 : 1;
}