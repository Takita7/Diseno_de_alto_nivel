// =============================================================================
// sc_main.cpp  –  Stage 3: Prueba del Bus TLM
//
// Topología de este stage:
//
//   Tester.init_socket  →  Bus.cpu_socket
//   Bus.ram_socket      →  RAM.socket
//   Bus.accel_out_socket → AccelStub.socket
//
// Tests:
//   1. Write/read a dirección RAM  → llega a RAM
//   2. Write a registro Acelerador → llega a AccelStub con offset correcto
//   3. Dirección fuera de mapa     → TLM_ADDRESS_ERROR_RESPONSE
//   4. Imagen completa Storage → Bus → RAM  (regresión del Stage 2)
// =============================================================================
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <tlm_utils/simple_target_socket.h>
#include "storage.h"
#include "ram.h"
#include "bus.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

#define CLR_OK   "\033[32m"
#define CLR_ERR  "\033[31m"
#define CLR_INFO "\033[36m"
#define CLR_RST  "\033[0m"

// =============================================================================
// AccelStub  –  reemplaza al Acelerador real hasta que lo implementemos.
// Registra la última transacción recibida para que el test pueda verificarla.
// =============================================================================
SC_MODULE(AccelStub) {

    tlm_utils::simple_target_socket<AccelStub> socket;

    // Estado observable para los tests
    uint64_t     last_addr   = 0;
    uint32_t     last_data   = 0;
    unsigned int last_len    = 0;
    bool         was_written = false;

    SC_CTOR(AccelStub) : socket("socket") {
        socket.register_b_transport(this, &AccelStub::b_transport);
        SC_REPORT_INFO("AccelStub", "Stub del Acelerador listo");
    }

    void b_transport(tlm::tlm_generic_payload& trans, sc_core::sc_time& /*delay*/) {
        last_addr   = trans.get_address();
        last_len    = trans.get_data_length();
        was_written = (trans.get_command() == tlm::TLM_WRITE_COMMAND);
        if (last_len <= 4 && trans.get_data_ptr())
            std::memcpy(&last_data, trans.get_data_ptr(), last_len);
        trans.set_response_status(tlm::TLM_OK_RESPONSE);
    }
};

// =============================================================================
// Tester  –  preview del CPU: tiene initiator socket y corre los tests
// =============================================================================
SC_MODULE(Tester) {

    tlm_utils::simple_initiator_socket<Tester> init_socket;

    PersistentStorage* storage  = nullptr;
    AccelStub*         accel_stub = nullptr;

    SC_CTOR(Tester) : init_socket("init_socket") {
        SC_THREAD(run);
    }

    // ── helpers TLM ──────────────────────────────────────────────────────────
    void do_write(uint64_t addr, uint8_t* data, unsigned int len) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        init_socket->b_transport(trans, delay);
        assert(trans.is_response_ok() && "do_write: transacción falló");
    }

    void do_read(uint64_t addr, uint8_t* data, unsigned int len) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        init_socket->b_transport(trans, delay);
        assert(trans.is_response_ok() && "do_read: transacción falló");
    }

    // Versión que devuelve el response status (para probar errores)
    tlm::tlm_response_status do_write_check(uint64_t addr, uint8_t* data, unsigned int len) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        init_socket->b_transport(trans, delay);
        return trans.get_response_status();
    }

    // ── SC_THREAD con los tests ───────────────────────────────────────────────
    void run() {
        std::cout << CLR_INFO
                  << "\n=== TLM Image Processor – Stage 3: Bus ==="
                  << CLR_RST << "\n";

        // ── TEST 1: Write/read a RAM a través del Bus ─────────────────────────
        std::cout << "\n[TEST 1] Write/Read RAM via Bus (addr 0x00001000)...\n";
        {
            uint8_t w[4] = {0xCA, 0xFE, 0xBA, 0xBE};
            uint8_t r[4] = {0x00, 0x00, 0x00, 0x00};
            do_write(0x00001000, w, 4);
            do_read (0x00001000, r, 4);
            assert(std::memcmp(w, r, 4) == 0);
            std::cout << CLR_OK << "  [OK] " << CLR_RST
                      << "0x" << std::hex << std::uppercase
                      << (int)r[0] << (int)r[1] << (int)r[2] << (int)r[3]
                      << std::dec << " en RAM[0x1000] ✓\n";
        }

        // ── TEST 2: Write a registro del Acelerador ───────────────────────────
        // El Bus debe restar la base (0x04000000) antes de enviar al Acelerador
        std::cout << "\n[TEST 2] Write a registro Acelerador (addr 0x04000008)...\n";
        {
            uint32_t num_pixels = 1920 * 1080;  // 2,073,600
            uint8_t* ptr = reinterpret_cast<uint8_t*>(&num_pixels);
            do_write(MemoryMap::ACCEL_BASE + 0x08, ptr, 4);

            assert(accel_stub->was_written);
            // El stub debe haber recibido el offset relativo (0x08), no 0x04000008
            assert(accel_stub->last_addr == 0x08);
            assert(accel_stub->last_data == num_pixels);

            std::cout << CLR_OK << "  [OK] " << CLR_RST
                      << "AccelStub recibió addr=0x" << std::hex << accel_stub->last_addr
                      << " data=" << std::dec << accel_stub->last_data
                      << " (offset correcto ✓)\n";
        }

        // ── TEST 3: Dirección fuera del mapa de memoria ───────────────────────
        std::cout << "\n[TEST 3] Dirección fuera de mapa (0x08000000)...\n";
        {
            uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            auto status = do_write_check(0x08000000, buf, 4);
            assert(status == tlm::TLM_ADDRESS_ERROR_RESPONSE);
            std::cout << CLR_OK << "  [OK] " << CLR_RST
                      << "TLM_ADDRESS_ERROR_RESPONSE recibido ✓\n";
        }

        // ── TEST 4: Imagen completa Storage → Bus → RAM (regresión Stage 2) ──
        std::cout << "\n[TEST 4] Storage → Bus → RAM (6.2 MB)...\n";
        {
            assert(storage != nullptr);
            std::vector<uint8_t> img = storage->load_image("images/input.raw");
            assert(img.size() == ImageConfig::RGB_SIZE);

            const unsigned int BLOCK = 4096;
            for (size_t off = 0; off < img.size(); off += BLOCK) {
                unsigned int chunk = static_cast<unsigned int>(
                    std::min((size_t)BLOCK, img.size() - off));
                do_write(off, img.data() + off, chunk);
            }

            std::vector<uint8_t> readback(img.size());
            for (size_t off = 0; off < readback.size(); off += BLOCK) {
                unsigned int chunk = static_cast<unsigned int>(
                    std::min((size_t)BLOCK, readback.size() - off));
                do_read(off, readback.data() + off, chunk);
            }

            assert(readback == img);
            std::cout << CLR_OK << "  [OK] " << CLR_RST
                      << img.size() << " bytes Storage → Bus → RAM → Bus ✓\n";
        }

        std::cout << CLR_INFO
                  << "\n=== Stage 3 COMPLETO | t=" << sc_core::sc_time_stamp()
                  << " ===" << CLR_RST << "\n\n";
    }
};

// =============================================================================
int sc_main(int /*argc*/, char* /*argv*/[]) {

    PersistentStorage storage  ("storage");
    RAM               ram      ("ram");
    Bus               bus      ("bus");
    AccelStub         accel_stub("accel_stub");
    Tester            tester   ("tester");

    tester.storage    = &storage;
    tester.accel_stub = &accel_stub;

    // Topología de conexión:
    //   Tester → Bus → RAM
    //          ↘ Bus → AccelStub
    tester.init_socket    .bind(bus.cpu_socket);
    bus.ram_socket        .bind(ram.socket);
    bus.accel_out_socket  .bind(accel_stub.socket);

    sc_core::sc_start();

    return 0;
}