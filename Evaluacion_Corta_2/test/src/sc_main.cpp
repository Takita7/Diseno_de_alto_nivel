// =============================================================================
// sc_main.cpp  –  Stage 4: Prueba del CPU (pasos 1 y 2)
//
// Topología:
//   CPU.socket → Bus.cpu_socket → Bus.ram_socket    → RAM.socket
//                               → Bus.accel_out_socket → AccelStub.socket
//
// Tests:
//   El CPU corre su SC_THREAD completo (pasos 1 y 2 reales, 3-6 stubs).
//   Tras sc_stop(), sc_main verifica los datos en RAM usando ram.peek()
//   (acceso de debug directo, sin necesidad de socket ni SC_THREAD).
// =============================================================================
#include <systemc.h>
#include <tlm_utils/simple_target_socket.h>
#include "storage.h"
#include "RAM.h"
#include "Bus.h"
#include "CPU.h"
#include "Accelerator.h"

#include <cassert>
#include <iostream>
#include <iomanip>

#define CLR_OK   "\033[32m"
#define CLR_ERR  "\033[31m"
#define CLR_INFO "\033[36m"
#define CLR_RST  "\033[0m"

// =============================================================================
// Test del sistema completo
// =============================================================================
int sc_main(int /*argc*/, char* /*argv*/[]) {

    std::cout << CLR_INFO
              << "\n=== TLM Image Processor – Stage 4: CPU (pasos 1 y 2) ==="
              << CLR_RST << "\n";

    // ── Instanciar módulos ────────────────────────────────────────────────────
    PersistentStorage storage  ("storage");
    RAM               ram      ("ram");
    Bus               bus      ("bus");
    Accelerator       accel    ("accelerator");
    CPU               cpu      ("cpu");

    cpu.set_storage(&storage);

    // ── Conectar sockets ─────────────────────────────────────────────────────
    cpu.socket           .bind(bus.cpu_socket);
    bus.ram_socket       .bind(ram.socket);
    bus.accel_out_socket .bind(accel.cfg_socket);
    accel.mem_socket     .bind(bus.accel_in_socket);

    // ── Ejecutar simulación ───────────────────────────────────────────────────
    // El CPU corre su SC_THREAD y llama sc_stop() al terminar los pasos 1 y 2
    sc_core::sc_start();

    // Verificación post-simulación con ram.peek() y archivo de salida
    std::cout << CLR_INFO << "\n[VERIFY] Verificando salida y memoria final..." << CLR_RST << "\n";

    std::vector<uint8_t> output = storage.load_image("images/output.raw");
    std::cout << "  Archivo de salida cargado: images/output.raw\n";
    std::cout << "  Tamaño archivo salida = " << output.size() << " bytes\n";
    assert(output.size() == ImageConfig::GRAY_SIZE && "Tamaño de salida incorrecto");

    uint8_t ram_head[4] = {};
    ram.peek(ImageMap::OUTPUT_BASE, ram_head, 4);
    std::cout << "  Primeros 4 bytes de salida en RAM [0x" << std::hex << ImageMap::OUTPUT_BASE << "] ="
              << std::dec << " (" << (int)ram_head[0] << ", " << (int)ram_head[1] << ", "
              << (int)ram_head[2] << ", " << (int)ram_head[3] << ")\n";

    bool start_ok = std::memcmp(ram_head, output.data(), 4) == 0;
    std::cout << "  Primeros 4 bytes RAM vs archivo salida: " << (start_ok ? "OK" : "ERROR") << "\n";

    uint8_t ram_tail[4] = {};
    ram.peek(ImageMap::OUTPUT_BASE + ImageConfig::GRAY_SIZE - 4, ram_tail, 4);
    uint8_t* out_tail = output.data() + ImageConfig::GRAY_SIZE - 4;
    bool tail_ok = std::memcmp(ram_tail, out_tail, 4) == 0;
    std::cout << "  Últimos 4 bytes RAM vs archivo salida: " << (tail_ok ? "OK" : "ERROR") << "\n";

    std::cout << "  Salida [0..3] = (" << (int)output[0] << ", " << (int)output[1] << ", "
              << (int)output[2] << ", " << (int)output[3] << ")\n";
    std::cout << "  Salida [end-4..end-1] = (" << (int)out_tail[0] << ", " << (int)out_tail[1] << ", "
              << (int)out_tail[2] << ", " << (int)out_tail[3] << ")\n";

    assert(start_ok && tail_ok && "La salida en RAM no coincide con el archivo guardado");

    std::cout << CLR_OK << "\n  [OK] " << CLR_RST
              << "Salida verificada en RAM y archivo de salida.\n";

    std::cout << CLR_INFO
              << "\n=== Stage 4 COMPLETO | t=" << sc_core::sc_time_stamp()
              << " ===" << CLR_RST << "\n\n";

    return 0;
}
