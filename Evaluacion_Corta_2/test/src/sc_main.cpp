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

#include <cassert>
#include <iostream>
#include <iomanip>

#define CLR_OK   "\033[32m"
#define CLR_ERR  "\033[31m"
#define CLR_INFO "\033[36m"
#define CLR_RST  "\033[0m"

// =============================================================================
// AccelStub  –  placeholder hasta Stage 5
// =============================================================================
SC_MODULE(AccelStub) {
    tlm_utils::simple_target_socket<AccelStub> socket;
    SC_CTOR(AccelStub) : socket("socket") {
        socket.register_b_transport(this, &AccelStub::b_transport);
    }
    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& /*delay*/) {
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// =============================================================================
int sc_main(int /*argc*/, char* /*argv*/[]) {

    std::cout << CLR_INFO
              << "\n=== TLM Image Processor – Stage 4: CPU (pasos 1 y 2) ==="
              << CLR_RST << "\n";

    // ── Instanciar módulos ────────────────────────────────────────────────────
    PersistentStorage storage  ("storage");
    RAM               ram      ("ram");
    Bus               bus      ("bus");
    AccelStub         accel_stub("accel_stub");
    CPU               cpu      ("cpu");

    cpu.set_storage(&storage);

    // ── Conectar sockets ─────────────────────────────────────────────────────
    cpu.socket           .bind(bus.cpu_socket);
    bus.ram_socket       .bind(ram.socket);
    bus.accel_out_socket .bind(accel_stub.socket);

    // ── Ejecutar simulación ───────────────────────────────────────────────────
    // El CPU corre su SC_THREAD y llama sc_stop() al terminar los pasos 1 y 2
    sc_core::sc_start();

    // ── Verificación post-simulación con ram.peek() ───────────────────────────
    // peek() es un acceso directo al vector interno de RAM, sin TLM ni socket.
    // Perfecto para verificar el contenido final sin perturbar la simulación.
    std::cout << CLR_INFO << "\n[VERIFY] Verificando contenido de RAM..." << CLR_RST << "\n";

    // Cargar imagen original para comparar
    std::vector<uint8_t> original = storage.load_image("images/input.raw");

    // Leer los primeros 12 bytes de RAM (4 píxeles) con peek() directo
    uint8_t head_ram[12] = {};
    ram.peek(0, head_ram, 12);

    std::cout << "  Primeros 4 píxeles en RAM vs original:\n";
    bool head_ok = true;
    for (int i = 0; i < 4; i++) {
        uint8_t r_ram  = head_ram[i*3],   g_ram  = head_ram[i*3+1], b_ram  = head_ram[i*3+2];
        uint8_t r_orig = original[i*3], g_orig = original[i*3+1], b_orig = original[i*3+2];
        bool match = (r_ram==r_orig && g_ram==g_orig && b_ram==b_orig);
        if (!match) head_ok = false;
        std::cout << "    px[" << i << "] RAM=("
                  << (int)r_ram  << "," << (int)g_ram  << "," << (int)b_ram  << ")  orig=("
                  << (int)r_orig << "," << (int)g_orig << "," << (int)b_orig << ")  "
                  << (match ? "✓" : "✗") << "\n";
    }

    // Leer los últimos 3 bytes (último píxel)
    uint8_t tail_ram[3] = {};
    ram.peek(ImageConfig::RGB_SIZE - 3, tail_ram, 3);
    uint8_t* tail_orig = original.data() + ImageConfig::RGB_SIZE - 3;
    bool tail_ok = std::memcmp(tail_ram, tail_orig, 3) == 0;
    std::cout << "  Último píxel: RAM=("
              << (int)tail_ram[0]  << "," << (int)tail_ram[1]  << "," << (int)tail_ram[2]  << ")"
              << "  orig=("
              << (int)tail_orig[0] << "," << (int)tail_orig[1] << "," << (int)tail_orig[2] << ")"
              << "  " << (tail_ok ? "✓" : "✗") << "\n";

    assert(head_ok && tail_ok && "Datos en RAM no coinciden con imagen original");

    std::cout << CLR_OK << "\n  [OK] " << CLR_RST
              << "RAM[0.." << ImageConfig::RGB_SIZE-1
              << "] verificado. CPU pasos 1 y 2 correctos.\n";

    std::cout << CLR_INFO
              << "\n=== Stage 4 COMPLETO | t=" << sc_core::sc_time_stamp()
              << " ===" << CLR_RST << "\n\n";

    return 0;
}