// =============================================================================
// CPU.h  –  Módulo CPU
//
// Controla el flujo completo del sistema:
//   1. Carga imagen RGB desde Storage
//   2. Escribe imagen en RAM vía Bus
//   3. Configura registros del Acelerador vía Bus   
//   4. Espera que el Acelerador termine             
//   5. Lee imagen gris desde RAM vía Bus            
//   6. Guarda imagen gris en Storage              
// =============================================================================
#pragma once

#include <systemc.h>                              
#include <tlm.h>                                  
#include <tlm_utils/simple_initiator_socket.h>
#include <vector>
#include <cstring>
#include <iostream>
#include "storage.h"                              
#include "bus.h"                                  

// -----------------------------------------------------------------------------
// Mapa de regiones de imagen dentro de la RAM
// (distinto de MemoryMap que define los rangos del Bus)
// -----------------------------------------------------------------------------
namespace ImageMap {
    constexpr uint64_t INPUT_BASE  = 0x00000000; // imagen RGB de entrada
    constexpr uint64_t OUTPUT_BASE = 0x00600000; 
}

// Registros del Acelerador (offsets relativos a MemoryMap::ACCEL_BASE)
namespace AccelReg {
    constexpr uint64_t SRC    = MemoryMap::ACCEL_BASE + 0x00; // dir. entrada
    constexpr uint64_t DST    = MemoryMap::ACCEL_BASE + 0x04; // dir. salida
    constexpr uint64_t CNT    = MemoryMap::ACCEL_BASE + 0x08; // num. píxeles
    constexpr uint64_t CTRL   = MemoryMap::ACCEL_BASE + 0x0C; // write 1 = start
    constexpr uint64_t STATUS = MemoryMap::ACCEL_BASE + 0x10; // read  2 = done
    constexpr uint32_t DONE   = 2;
}

// Tamaño de bloque por transacción TLM (64 KB)
static constexpr uint32_t CPU_CHUNK = 64u * 1024u;

// =============================================================================
SC_MODULE(CPU) {

    tlm_utils::simple_initiator_socket<CPU> socket;
    PersistentStorage* storage_ptr;               

    SC_CTOR(CPU)
        : socket("socket"),
          storage_ptr(nullptr)
    {
        SC_THREAD(run);
    }

    void set_storage(PersistentStorage* s) { storage_ptr = s; }

private:

    // -------------------------------------------------------------------------
    // Helpers TLM — encapsulan tlm_generic_payload para el código del flujo
    // -------------------------------------------------------------------------
    void tlm_write(uint64_t addr, const uint8_t* data, uint32_t len) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(tlm::TLM_WRITE_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(const_cast<uint8_t*>(data));
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        socket->b_transport(trans, delay);
        wait(delay);   // avanzar tiempo de simulación por el delay acumulado
        sc_assert(trans.is_response_ok());
    }

    void tlm_read(uint64_t addr, uint8_t* data, uint32_t len) {
        tlm::tlm_generic_payload trans;
        sc_core::sc_time delay = sc_core::SC_ZERO_TIME;
        trans.set_command(tlm::TLM_READ_COMMAND);
        trans.set_address(addr);
        trans.set_data_ptr(data);
        trans.set_data_length(len);
        trans.set_streaming_width(len);
        trans.set_byte_enable_ptr(nullptr);
        trans.set_dmi_allowed(false);
        trans.set_response_status(tlm::TLM_INCOMPLETE_RESPONSE);
        socket->b_transport(trans, delay);  
        wait(delay);
        sc_assert(trans.is_response_ok());
    }

    // Escribir/leer un registro de 32 bits (para configurar el Acelerador)
    void write_reg(uint64_t addr, uint32_t val) {
        tlm_write(addr, reinterpret_cast<const uint8_t*>(&val), 4);
    }

    uint32_t read_reg(uint64_t addr) {
        uint32_t val = 0;
        tlm_read(addr, reinterpret_cast<uint8_t*>(&val), 4);
        return val;                                            
    }

    // -------------------------------------------------------------------------
    // run  –  SC_THREAD con el flujo principal del sistema
    // -------------------------------------------------------------------------
    void run() {
        wait(sc_core::sc_time(10, sc_core::SC_NS)); 

        SC_REPORT_INFO("CPU", "Iniciando flujo de procesamiento");

        // PASO 1: Cargar imagen RGB desde Storage 
        std::cout << "\n[CPU] PASO 1: Cargando imagen desde disco\n";
        sc_assert(storage_ptr != nullptr);
        std::vector<uint8_t> rgb = storage_ptr->load_image("images/input.raw");
        sc_assert(rgb.size() == ImageConfig::RGB_SIZE);

        // PASO 2: Escribir imagen RGB en RAM 
        std::cout << "[CPU] PASO 2: Escribiendo imagen RGB en RAM desde Storage a RAM dir "
                  << std::hex << ImageMap::INPUT_BASE << std::dec << "\n";
        uint32_t offset = 0;
        while (offset < rgb.size()) {
            uint32_t sz = std::min(CPU_CHUNK,
                          static_cast<uint32_t>(rgb.size() - offset));
            tlm_write(ImageMap::INPUT_BASE + offset,
                      rgb.data() + offset, sz);                
            offset += sz;
        }
        std::cout << "[CPU] PASO 2: " << offset << " bytes escritos en RAM (RGB source) en dir 0x"
                  << std::hex << ImageMap::INPUT_BASE << "]\n" << std::dec;

        // PASO 3: Configurar Acelerador 
        std::cout << "[CPU] PASO 3: Configurar Acelerador (RGB source -> grayscale dest)\n";
        write_reg(AccelReg::SRC, static_cast<uint32_t>(ImageMap::INPUT_BASE));
        write_reg(AccelReg::DST, static_cast<uint32_t>(ImageMap::OUTPUT_BASE));
        write_reg(AccelReg::CNT, ImageConfig::WIDTH * ImageConfig::HEIGHT);
        
        // PASO 4: Iniciar Acelerador y esperar 
        std::cout << "[CPU] PASO 4: Iniciar acelerador y esperar\n";
        write_reg(AccelReg::CTRL, 1);
        while (read_reg(AccelReg::STATUS) != AccelReg::DONE)
            wait(sc_core::sc_time(500, sc_core::SC_NS));

        // PASO 5: Leer imagen gris desde RAM
        std::cout << "[CPU] PASO 5: Leyendo imagen grayscale desde RAM dir 0x"
                  << std::hex << ImageMap::OUTPUT_BASE << std::dec << "\n";
        std::vector<uint8_t> gray(ImageConfig::GRAY_SIZE);
        offset = 0;
        while (offset < gray.size()) {
            uint32_t sz = std::min(CPU_CHUNK,
                          static_cast<uint32_t>(gray.size() - offset));
            tlm_read(ImageMap::OUTPUT_BASE + offset, gray.data() + offset, sz);
            offset += sz;
        }

        // PASO 6: Guardar imagen gris en disco 
        std::cout << "[CPU] PASO 6: Guardando imagen grayscale de salida en images/output.raw\n";
        storage_ptr->save_image("images/output.raw", gray);

        std::cout << "\n[CPU] Todo el procesamiento del CPU ha terminado. Deteniendo simulación"
                  << sc_core::sc_time_stamp() << "\n";
        sc_core::sc_stop();
    }
};