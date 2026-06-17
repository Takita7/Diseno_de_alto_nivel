// =============================================================================
// sc_main.cpp  –  Stage 2: Prueba del módulo RAM
//
// El error anterior: socket-> en un simple_target_socket da la interfaz
// *backward* (tlm_bw_transport_if), no la forward. En TLM 2.0 las
// transacciones siempre van de un initiator_socket → target_socket.
//
// Fix: crear un módulo Tester con simple_initiator_socket que se bindea
// al socket de la RAM. Esto también es un preview de cómo quedará el CPU.
// =============================================================================
#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include "storage.h"
#include "RAM.h"

#include <cassert>
#include <iomanip>
#include <iostream>

#define CLR_OK   "\033[32m"
#define CLR_ERR  "\033[31m"
#define CLR_INFO "\033[36m"
#define CLR_RST  "\033[0m"

// =============================================================================
// Módulo Tester  –  simula al CPU: tiene un initiator socket y corre pruebas
// en un SC_THREAD. Cuando implementemos el CPU real, tendrá exactamente
// esta misma estructura.
// =============================================================================
SC_MODULE(Tester) {

    tlm_utils::simple_initiator_socket<Tester> init_socket;

    // Puntero al storage (acceso directo, sin TLM)
    PersistentStorage* storage;

    SC_CTOR(Tester) : init_socket("init_socket"), storage(nullptr) {
        SC_THREAD(run);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers TLM — en el CPU final estos serán métodos privados también
    // ─────────────────────────────────────────────────────────────────────────
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
        init_socket->b_transport(trans, delay);  // ← initiator->  es la fw IF
        assert(trans.is_response_ok() && "RAM write falló");
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
        assert(trans.is_response_ok() && "RAM read falló");
    }

    // ─────────────────────────────────────────────────────────────────────────
    // SC_THREAD: corre los tests en orden
    // ─────────────────────────────────────────────────────────────────────────
    void run() {
        std::cout << CLR_INFO
                  << "\n=== TLM Image Processor – Stage 2: RAM ==="
                  << CLR_RST << "\n";

        // ── TEST 1: escribir y leer patrón de 4 bytes ────────────────────────
        std::cout << "\n[TEST 1] Escribir/leer patrón en addr 0x00001000...\n";
        {
            uint8_t write_buf[4] = {0xDE, 0xAD, 0xBE, 0xEF};
            uint8_t read_buf[4]  = {0x00, 0x00, 0x00, 0x00};

            do_write(0x1000, write_buf, 4);
            do_read (0x1000, read_buf,  4);

            assert(std::memcmp(write_buf, read_buf, 4) == 0);
            std::cout << CLR_OK << "  [OK] " << CLR_RST
                      << "0x" << std::hex << std::uppercase
                      << (int)read_buf[0] << (int)read_buf[1]
                      << (int)read_buf[2] << (int)read_buf[3]
                      << std::dec << " leído correctamente\n";
        }

        // ── TEST 2: acceso fuera de rango debe devolver error ─────────────────
        std::cout << "\n[TEST 2] Acceso fuera de rango (debe retornar error)...\n";
        {
            uint8_t buf[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            tlm::tlm_generic_payload trans;
            sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
            trans.set_command(tlm::TLM_WRITE_COMMAND);
            trans.set_address(RAM_SIZE - 2);  // 2 bytes antes del fin, len=4 → sale
            trans.set_data_ptr(buf);
            trans.set_data_length(4);
            trans.set_streaming_width(4);
            trans.set_byte_enable_ptr(nullptr);
            trans.set_dmi_allowed(false);
            init_socket->b_transport(trans, delay);

            assert(trans.get_response_status() == tlm::TLM_ADDRESS_ERROR_RESPONSE);
            std::cout << CLR_OK << "  [OK] " << CLR_RST
                      << "TLM_ADDRESS_ERROR_RESPONSE recibido correctamente\n";
        }

        // ── TEST 3: cargar imagen real y escribirla a RAM ─────────────────────
        std::cout << "\n[TEST 3] Storage → RAM[0x00000000] (6.2 MB en bloques 4KB)...\n";
        {
            assert(storage != nullptr && "storage no fue asignado");
            std::vector<uint8_t> img = storage->load_image("images/input.raw");
            assert(img.size() == ImageConfig::RGB_SIZE);

            const unsigned int BLOCK = 4096;
            for (size_t offset = 0; offset < img.size(); offset += BLOCK) {
                unsigned int chunk = static_cast<unsigned int>(
                    std::min((size_t)BLOCK, img.size() - offset));
                do_write(offset, img.data() + offset, chunk);
            }
            std::cout << "  Escrita en " << (img.size() + BLOCK - 1) / BLOCK
                      << " bloques de 4 KB\n";

            // Leer de vuelta y comparar
            std::vector<uint8_t> readback(img.size());
            for (size_t offset = 0; offset < readback.size(); offset += BLOCK) {
                unsigned int chunk = static_cast<unsigned int>(
                    std::min((size_t)BLOCK, readback.size() - offset));
                do_read(offset, readback.data() + offset, chunk);
            }

            assert(readback == img && "Datos en RAM no coinciden con imagen");
            std::cout << CLR_OK << "  [OK] " << CLR_RST
                      << img.size() << " bytes en RAM == archivo original\n";
        }

        std::cout << CLR_INFO
                  << "\n=== Stage 2 COMPLETO | t=" << sc_core::sc_time_stamp()
                  << " ===" << CLR_RST << "\n\n";
    }
};

// =============================================================================
int sc_main(int /*argc*/, char* /*argv*/[]) {

    PersistentStorage storage("storage");
    RAM               ram("ram");
    Tester            tester("tester");

    // Pasar referencia del storage al tester (acceso directo, sin TLM)
    tester.storage = &storage;

    // Bindear el initiator socket del Tester al target socket de la RAM
    // Cuando exista el Bus, será: tester.init_socket → bus.socket
    tester.init_socket.bind(ram.socket);

    sc_core::sc_start();  // corre hasta que no haya más eventos (SC_THREAD termina)

    return 0;
}