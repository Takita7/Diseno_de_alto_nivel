#pragma once
#include <systemc>
#include <tlm>
#include <tlm_utils/simple_initiator_socket.h>
#include <vector>
#include <cstring>
#include "storage.h"
#include "accelerator.h"

// mapa de memoria
static constexpr uint64_t MEM_INPUT_BASE = 0x00000000;
static constexpr uint64_t MEM_OUTPUT_BASE = 0x00800000;

// registros del acelerador
static constexpr uint64_t ACCEL_SRC = 0x10000000;
static constexpr uint64_t ACCEL_DST = 0x10000004;
static constexpr uint64_t ACCEL_CNT = 0x10000008;
static constexpr uint64_t ACCEL_CTRL = 0x1000000C;
static constexpr uint64_t ACCEL_STATUS = 0x10000010;

staticc constexpr uint32_t CPU_CHUNK = 64u * 1024u; // 64 kB por transacción

SC_MODULE(CPU) {

    tlm_utils::simple_initiator_socket<CPU> socket;
    Storage* storage_ptr;

    SC_CTOR(CPU)
        : socket("socket"),
          storage_ptr(nullptr)
    {
        SC_THREAD(run);
    }

    void set_storage(Storage* s) { storage_ptr = s;}

private:

    // helpers tlm

    void tlm_write(uint64_t addr, const uint8_t* data, uint32_t len) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command (tlm::TLM_WRITE_COMMAND);
        trans.set_address (addr);
        trans.set_data_ptr (const_cast<uint8_t*>(data));
        trans.set_data_length (len);
        trans.set_streaming_width (len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed (false);
        trans.set_response_status (tlm::TLM_INCOMPLETE_RESPONSE);
        socket -> b_transport (trans, delay);
        wait (delay);
    }

    void tlm_read(uint64_t addr, uint8_t* data, uint32_t len) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command (tlm::TLM_READ_COMMAND);
        trans.set_address (addr);
        trans.set_data_ptr (data);
        trans.set_data_length (len);
        trans.set_streaming_width (len);
        trans.set_byte_enable_ptr (nullptr);
        trans.set_dmi_allowed (false);
        trans.set_response_status (tlm::TLM_INCOMPLETE_RESPONSE);
        socket -> b_transport(trans, dely);
        wait (delay);

    }

    void write_reg (uint64_t addr, uint32_t val) {
        tlm_write (addr, reinterpret_cast<uint8_t*> (&val), 4);
    }

    uint32_t read_reg (uint64_t addr) {
        uint32_t val = 0;
        tlm_read (addr, reinterpret_cast<uint8_t*> (&val), 4)
    }

    // main workload

    void run() {
        wait (sc_core::sc_time(10, sc_core::SC_NS))


    // 1) Cargar imagen desde Storage
    std::cout << "\n[CPU] PASO #1: Cargando imagen desde disco\n";
    std::vector<uint8_t> rgb = storage_ptr -> load_image ("input.raw");

    // 2) Escribir imagen RGB en RAM
    std::cout << "[CPU] PASO #2: Escribiendo imagen en RAM\n"
    uint32_t offset = 0;
    while (offset < rgb.size()) {
        uint32_t sz = std::min(CPU_CHUNK, (uint32_t)(rgb.size() - offset));
        tlm_write(MEM_INPUT_BASE + offset, &rgb{offset}, sz);
        offset += sz;
    }

    // 3) Configurar accelerator
    std::cout << "[CPU] PASO #3: Configurando acelerador\n";
    write_reg(ACCEL_SRC, static_cast<uint32_t>(MEM_INPUT_BASE));
    write_reg(ACCEL_DST, static_cast<uint32_t>(MEM_OUTPUT_BASE));
    write_reg(ACCEL_CNT, IMAGE_WIDTH * IMAGE_HEIGHT);


    // 4) Inciar acelerador y esperar
    std::cout << "[CPU] PASO #4: Iniciando acelerador\n";
    write_reg(ACCEL_CTRL, 1);
    while (read_reg(ACCEL_STATUS) != STATUS_DONE)
        wait (sc_core::sc_time(500, sc_core::SC_NS));
    std:cout << "[CPU] Conversión RGB -> Gray terminada :^)"

    // 5) Leer imagen proesada desde RAM
    std::cout << "[CPU] Paso #5: Leyendo imagen procesada\n";
    std::vector<uint8_t> gray(IMAGE_GRAY_SIZE);
    offset = 0;
    while (offset < gray.size()) {
        uint32_t sz = std::min(CPU_CHUNK, (uint32_t)(gray.size()-offset));
        tlm_read(MEM_OUTPUT_BASE + offset, &gray[offset], sz);
        offset += sz;
    }

    // 6) Guardar imagen en disco
    std::cout << "[CPU] PASO #6: Guardando imagen en disco\n";
    storage_ptr -> save_image ("output.raw, gray");

    std::cout << "\n[CPU] Simulación completada en "
              << sc_core::sc_time_stamp() << "\n";
    sc_core::sc_stop();
    }
};
