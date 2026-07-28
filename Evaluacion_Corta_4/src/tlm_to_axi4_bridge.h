// =============================================================================
// tlm_to_axi4_bridge.h  –  Puente TLM 2.0 -> AXI4 Full
//
// Recibe transacciones TLM bloqueantes desde el Bus y las convierte en
// transacciones AXI4 usando los 5 canales (AW, W, B, AR, R).
//
// Transacciones largas (> AXI4_MAX_BURST x AXI4_DATA_BYTES bytes) se
// dividen automáticamente en múltiples bursts INCR.
//
// Conexión (desde top_test):
//   Bus.ram_socket  ->  target_socket
//   aw_port  ->  sc_fifo<AXI4_AW>  ->  RAM_AXI4_Model.aw_port
//   w_port   ->  sc_fifo<AXI4_W>   ->  RAM_AXI4_Model.w_port
//   b_port   <-  sc_fifo<AXI4_B>   <-  RAM_AXI4_Model.b_port
//   ar_port  ->  sc_fifo<AXI4_AR>  ->  RAM_AXI4_Model.ar_port
//   r_port   <-  sc_fifo<AXI4_R>   <-  RAM_AXI4_Model.r_port
// =============================================================================
#pragma once

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include "axi4_if.h"

SC_MODULE(TLM_to_AXI4_Bridge)
{
    // ── Lado TLM (conecta al Bus) ─────────────────────────────────────────────
    tlm_utils::simple_target_socket<TLM_to_AXI4_Bridge> target_socket;

    // ── Lado AXI4 (conecta a RAM_AXI4_Model vía sc_fifo) ────────────────────
    sc_core::sc_fifo_out<AXI4_AW> aw_port;
    sc_core::sc_fifo_out<AXI4_W>  w_port;
    sc_core::sc_fifo_in <AXI4_B>  b_port;
    sc_core::sc_fifo_out<AXI4_AR> ar_port;
    sc_core::sc_fifo_in <AXI4_R>  r_port;

    SC_CTOR(TLM_to_AXI4_Bridge)
        : target_socket("target_socket")
        , aw_port("aw_port"), w_port("w_port"), b_port("b_port")
        , ar_port("ar_port"), r_port("r_port")
        , txn_id_(0)
    {
        target_socket.register_b_transport(this, &TLM_to_AXI4_Bridge::b_transport);
        SC_REPORT_INFO("TLM_to_AXI4_Bridge", "Bridge TLM→AXI4 creado");
    }

private:
    uint32_t txn_id_;   // ID incremental por transacción AXI4

    // =========================================================================
    // b_transport  –  punto de entrada TLM
    // Divide la transacción en bursts AXI4 de hasta AXI4_MAX_BURST beats.
    // =========================================================================
    void b_transport(tlm::tlm_generic_payload& trans,
                     sc_core::sc_time& delay)
    {
        const uint64_t addr = trans.get_address();
        uint8_t* const data = trans.get_data_ptr();
        const uint32_t len  = trans.get_data_length();
        const tlm::tlm_command cmd = trans.get_command();

        if (data == nullptr || len == 0) {
            trans.set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
            return;
        }

        bool ok     = true;
        uint32_t off = 0;

        // Partir en bursts si la transacción es mayor a lo que AXI4 permite
        while (off < len && ok) {
            const uint32_t remaining   = len - off;
            const uint32_t burst_bytes = std::min(remaining,
                                         AXI4_MAX_BURST * AXI4_DATA_BYTES);

            // Redondear hacia arriba al siguiente beat completo
            const uint32_t beats =
                (burst_bytes + AXI4_DATA_BYTES - 1) / AXI4_DATA_BYTES;

            const uint32_t id = txn_id_++;

            if (cmd == tlm::TLM_WRITE_COMMAND)
                ok = do_write(addr + off, data + off, burst_bytes, beats, id, delay);
            else
                ok = do_read (addr + off, data + off, burst_bytes, beats, id, delay);

            off += burst_bytes;
        }

        trans.set_response_status(ok ? tlm::TLM_OK_RESPONSE
                                     : tlm::TLM_GENERIC_ERROR_RESPONSE);
    }

    // =========================================================================
    // do_write  –  AW -> W beats -> esperar B
    // =========================================================================
    bool do_write(uint64_t addr, const uint8_t* data,
                  uint32_t bytes, uint32_t beats, uint32_t id,
                  sc_core::sc_time& delay)
    {
        // 1. Enviar Write Address
        AXI4_AW aw;
        aw.awid    = id;
        aw.awaddr  = addr;
        aw.awlen   = static_cast<uint8_t>(beats - 1);
        aw.awsize  = 2;   // 2^2 = 4 bytes por beat
        aw.awburst = AXI4_BURST_INCR;
        aw_port.write(aw);

        // 2. Enviar Write Data beats
        for (uint32_t i = 0; i < beats; ++i) {
            AXI4_W w;
            w.wlast = (i == beats - 1);
            w.wstrb = 0xF;  // todos los bytes válidos por defecto

            const uint32_t beat_off = i * AXI4_DATA_BYTES;
            for (uint32_t b = 0; b < AXI4_DATA_BYTES; ++b) {
                const uint32_t src = beat_off + b;
                if (src < bytes) {
                    w.wdata[b] = data[src];
                } else {
                    // Último beat parcial: padding + deshabilitar byte
                    w.wdata[b] = 0x00;
                    w.wstrb &= ~(1u << b);
                }
            }
            w_port.write(w);
        }

        // 3. Esperar Write Response
        const AXI4_B b = b_port.read();

        // Delay acumulado: beats × 10 ns (igual que RAM_AXI4_Model)
        delay += sc_core::sc_time(beats * 10, sc_core::SC_NS);

        if (b.bresp != AXI4_RESP_OKAY) {
            SC_REPORT_WARNING("TLM_to_AXI4_Bridge",
                              "Write response: SLVERR");
            return false;
        }
        return true;
    }

    // =========================================================================
    // do_read  –  AR -> esperar R beats
    // =========================================================================
    bool do_read(uint64_t addr, uint8_t* data,
                 uint32_t bytes, uint32_t beats, uint32_t id,
                 sc_core::sc_time& delay)
    {
        // 1. Enviar Read Address
        AXI4_AR ar;
        ar.arid    = id;
        ar.araddr  = addr;
        ar.arlen   = static_cast<uint8_t>(beats - 1);
        ar.arsize  = 2;
        ar.arburst = AXI4_BURST_INCR;
        ar_port.write(ar);

        // 2. Recolectar Read Data beats
        for (uint32_t i = 0; i < beats; ++i) {
            const AXI4_R r = r_port.read();

            if (r.rresp != AXI4_RESP_OKAY) {
                SC_REPORT_WARNING("TLM_to_AXI4_Bridge",
                                  "Read response: SLVERR");
                return false;
            }

            const uint32_t beat_off = i * AXI4_DATA_BYTES;
            for (uint32_t b = 0; b < AXI4_DATA_BYTES; ++b) {
                const uint32_t dst = beat_off + b;
                if (dst < bytes) data[dst] = r.rdata[b];
            }
        }

        delay += sc_core::sc_time(beats * 10, sc_core::SC_NS);
        return true;
    }
};