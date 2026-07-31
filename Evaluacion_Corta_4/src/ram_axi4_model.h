// =============================================================================
// ram_axi4_model.h  –  Modelo behavioral de RAM con interfaz AXI4 Full
//
// Reemplaza ram.h en Phase 2+. Recibe transacciones AXI4 a través de
// sc_fifo ports y las ejecuta sobre un buffer interno de 64 MB.
//
// Dos SC_THREADs independientes:
//   write_thread  –  consume AW + W beats, produce B
//   read_thread   –  consume AR, produce R beats
//
// Conexión (desde top_test):
//   sc_fifo<AXI4_AW>  ->  aw_port
//   sc_fifo<AXI4_W>   ->  w_port
//   b_port            ->  sc_fifo<AXI4_B>
//   sc_fifo<AXI4_AR>  ->  ar_port
//   r_port            ->  sc_fifo<AXI4_R>
// =============================================================================
#pragma once

#include <systemc.h>
#include <vector>
#include <sstream>
#include "axi4_if.h"

static constexpr uint32_t RAM_AXI4_SIZE = 64u * 1024u * 1024u; // 64 MB

SC_MODULE(RAM_AXI4_Model)
{
    // ── Puertos AXI4 ─────────────────────────────────────────────────────────
    sc_core::sc_fifo_in <AXI4_AW> aw_port;
    sc_core::sc_fifo_in <AXI4_W>  w_port;
    sc_core::sc_fifo_out<AXI4_B>  b_port;
    sc_core::sc_fifo_in <AXI4_AR> ar_port;
    sc_core::sc_fifo_out<AXI4_R>  r_port;

    SC_HAS_PROCESS(RAM_AXI4_Model);

    explicit RAM_AXI4_Model(sc_core::sc_module_name name)
        : sc_module(name)
        , aw_port("aw_port"), w_port("w_port"), b_port("b_port")
        , ar_port("ar_port"), r_port("r_port")
        , mem_(RAM_AXI4_SIZE, 0x00)
    {
        SC_THREAD(write_thread);
        SC_THREAD(read_thread);
        SC_REPORT_INFO("RAM_AXI4_Model", "Módulo RAM AXI4 creado (64 MB)");
    }

private:
    std::vector<uint8_t> mem_;

    // -------------------------------------------------------------------------
    // write_thread  –  AW + W -> operación de escritura -> B
    // -------------------------------------------------------------------------
    void write_thread()
    {
        while (true) {
            // 1. Leer canal AW (Write Address)
            AXI4_AW aw = aw_port.read();
            uint64_t addr  = aw.awaddr;
            uint32_t beats = static_cast<uint32_t>(aw.awlen) + 1;

            AXI4_B b;
            b.bid   = aw.awid;
            b.bresp = AXI4_RESP_OKAY;

            // 2. Leer beats del canal W (Write Data)
            for (uint32_t i = 0; i < beats; ++i) {
                AXI4_W w = w_port.read();

                // Aplicar byte enables (wstrb)
                for (uint32_t byte = 0; byte < AXI4_DATA_BYTES; ++byte) {
                    if ((w.wstrb >> byte) & 0x1) {
                        if (addr + byte < RAM_AXI4_SIZE) {
                            mem_[addr + byte] = w.wdata[byte];
                        } else {
                            b.bresp = AXI4_RESP_SLVERR;
                            std::ostringstream oss;
                            oss << "Write OOB: addr=0x" << std::hex << addr + byte;
                            SC_REPORT_WARNING("RAM_AXI4_Model", oss.str().c_str());
                        }
                    }
                }

                // INCR burst: avanzar dirección un beat
                if (aw.awburst == AXI4_BURST_INCR)
                    addr += AXI4_DATA_BYTES;

                // Latencia sintética: 1 ciclo por beat (10 ns)
                wait(sc_core::sc_time(10, sc_core::SC_NS));
            }

            // 3. Enviar respuesta canal B (Write Response)
            b_port.write(b);
        }
    }

    // -------------------------------------------------------------------------
    // read_thread  –  AR -> operación de lectura -> R beats
    // -------------------------------------------------------------------------
    void read_thread()
    {
        while (true) {
            // 1. Leer canal AR (Read Address)
            AXI4_AR ar = ar_port.read();
            uint64_t addr  = ar.araddr;
            uint32_t beats = static_cast<uint32_t>(ar.arlen) + 1;

            // 2. Enviar beats del canal R (Read Data)
            for (uint32_t i = 0; i < beats; ++i) {
                AXI4_R r;
                r.rid   = ar.arid;
                r.rresp = AXI4_RESP_OKAY;
                r.rlast = (i == beats - 1);

                for (uint32_t byte = 0; byte < AXI4_DATA_BYTES; ++byte) {
                    if (addr + byte < RAM_AXI4_SIZE) {
                        r.rdata[byte] = mem_[addr + byte];
                    } else {
                        r.rresp       = AXI4_RESP_SLVERR;
                        r.rdata[byte] = 0x00;
                        std::ostringstream oss;
                        oss << "Read OOB: addr=0x" << std::hex << addr + byte;
                        SC_REPORT_WARNING("RAM_AXI4_Model", oss.str().c_str());
                    }
                }

                if (ar.arburst == AXI4_BURST_INCR)
                    addr += AXI4_DATA_BYTES;

                // Latencia sintética: 1 ciclo por beat (10 ns)
                r_port.write(r);
                wait(sc_core::sc_time(10, sc_core::SC_NS));
            }
        }
    }
};