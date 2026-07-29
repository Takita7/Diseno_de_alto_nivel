// =============================================================================
// ram_rtl_sc.h  -  Wrapper SystemC sobre el modelo Verilator de ram_axi4.sv
//
// Expone el mismo simple_target_socket que ram.h y RAM_AXI4_Model,
// por lo que Bus, CPU y Accelerator NO necesitan ninguna modificacion.
//
// Internamente convierte transacciones TLM bloqueantes en ciclos AXI4
// senial por senial sobre Vram_axi4 (generado por Verilator --cc).
//
// El driver AXI4 es identico al de sim_main.cpp (ya validado):
//   - Patron pipelined en escritura (pre-drive beat 0 antes del loop)
//   - tick() llama wait() -> valido porque b_transport corre en SC_THREAD
//     del CPU (o Accelerator), no en un SC_METHOD
//
// Paths necesarios para compilar:
//   -I build/verilator_lib
//   -I $(VERILATOR_ROOT)/include
//   build/verilator_lib/Vram_axi4__ALL.a
// =============================================================================
#pragma once

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

// Headers generados por Verilator (build/verilator_lib/)
#include "Vram_axi4.h"
#include "verilated.h"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <sstream>

SC_MODULE(RAM_RTL_SC)
{
    // ---- Puerto TLM (identico a RAM::socket y TLM_to_AXI4_Bridge) ----------
    tlm_utils::simple_target_socket<RAM_RTL_SC> socket;

    SC_HAS_PROCESS(RAM_RTL_SC);

    explicit RAM_RTL_SC(sc_core::sc_module_name name)
        : sc_module(name)
        , socket("socket")
        , txn_id_(0)
    {
        // Crear contexto y modelo Verilator
        ctx_ = new VerilatedContext;
        ctx_->traceEverOn(false);
        dut_ = new Vram_axi4{ctx_};

        socket.register_b_transport(this, &RAM_RTL_SC::b_transport);

        // Inicializar seniales y aplicar reset ANTES de que empiece la
        // simulacio
        init_signals();
        apply_reset_static();

        SC_REPORT_INFO("RAM_RTL_SC", "RAM RTL Verilator lista (64 MB)");
    }

    ~RAM_RTL_SC() {
        dut_->final();
        delete dut_;
        delete ctx_;
    }

private:
    VerilatedContext* ctx_;
    Vram_axi4*        dut_;
    uint8_t           txn_id_;

    // =========================================================================
    // tick()
    // Avanza un ciclo de clock (10 ns: 5 ns high + 5 ns low).
    // Valido desde b_transport porque se llama en el contexto del
    // SC_THREAD del CPU o del Accelerator.
    // =========================================================================
    void tick()
    {
        dut_->clk = 1; dut_->eval();
        wait(sc_core::sc_time(5, sc_core::SC_NS));

        dut_->clk = 0; dut_->eval();
        wait(sc_core::sc_time(5, sc_core::SC_NS));
    }

    // =========================================================================
    // Inicializacion sin wait() — para usar en el constructor
    // =========================================================================
    void init_signals()
    {
        dut_->clk     = 0; dut_->rst     = 0;
        dut_->awvalid = 0; dut_->awid    = 0;
        dut_->awaddr  = 0; dut_->awlen   = 0;
        dut_->awsize  = 0; dut_->awburst = 0;
        dut_->wvalid  = 0; dut_->wdata   = 0;
        dut_->wstrb   = 0; dut_->wlast   = 0;
        dut_->bready  = 0;
        dut_->arvalid = 0; dut_->arid    = 0;
        dut_->araddr  = 0; dut_->arlen   = 0;
        dut_->arsize  = 0; dut_->arburst = 0;
        dut_->rready  = 0;
        dut_->eval();
    }

    // Reset estatico (sin wait): cicla el clock manualmente sin avanzar
    // el tiempo de SystemC. 
    void apply_reset_static()
    {
        dut_->rst = 0;
        for (int i = 0; i < 5; i++) {
            dut_->clk = 0; dut_->eval();
            dut_->clk = 1; dut_->eval();
        }
        dut_->rst = 1;
        dut_->clk = 0; dut_->eval();
        dut_->clk = 1; dut_->eval();
        dut_->clk = 0; dut_->eval();
    }

    // =========================================================================
    // b_transport
    // Convierte la transaccion TLM en uno o varios bursts AXI4 INCR.
    // Transacciones > 1024 bytes se dividen en multiples bursts.
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

        constexpr uint32_t BEAT_BYTES = 4;
        constexpr uint32_t MAX_BURST  = 256;
        constexpr uint32_t MAX_BYTES  = MAX_BURST * BEAT_BYTES; // 1024

        bool     ok     = true;
        uint32_t offset = 0;

        while (offset < len && ok) {
            uint32_t burst_bytes = std::min(len - offset, MAX_BYTES);
            uint32_t beats = (burst_bytes + BEAT_BYTES - 1) / BEAT_BYTES;
            uint32_t baddr = static_cast<uint32_t>(addr + offset);

            if (cmd == tlm::TLM_WRITE_COMMAND)
                ok = do_write(baddr, data + offset, burst_bytes, beats);
            else
                ok = do_read (baddr, data + offset, burst_bytes, beats);

            offset += burst_bytes;
        }

        // Latencia acumulada: 1 ciclo (10 ns) por beat
        const uint32_t total_beats = (len + BEAT_BYTES - 1) / BEAT_BYTES;
        delay += sc_core::sc_time(total_beats * 10, sc_core::SC_NS);

        trans.set_response_status(ok ? tlm::TLM_OK_RESPONSE
                                     : tlm::TLM_GENERIC_ERROR_RESPONSE);
    }

    // =========================================================================
    // do_write  —  AW + W pipelined + B
    // =========================================================================
    bool do_write(uint32_t addr, const uint8_t* data,
                  uint32_t bytes, uint32_t beats)
    {
        const uint8_t id = txn_id_++ & 0xF;

        // -- Write Address --
        dut_->awid    = id;
        dut_->awaddr  = addr;
        dut_->awlen   = static_cast<uint8_t>(beats - 1);
        dut_->awsize  = 2;       // 2^2 = 4 bytes/beat
        dut_->awburst = 0x1;     // INCR
        dut_->awvalid = 1;

        while (!dut_->awready) tick();
        tick();                   
        dut_->awvalid = 0;

        // -- Write Data pipelined --
        // Empaqueta 4 bytes del buffer en una palabra de 32 bits y genera wstrb
        auto pack = [&](uint32_t beat, uint8_t& strb) -> uint32_t {
            uint32_t word = 0; strb = 0;
            for (uint32_t b = 0; b < 4; b++) {
                uint32_t src = beat * 4 + b;
                if (src < bytes) {
                    word |= static_cast<uint32_t>(data[src]) << (b * 8);
                    strb |= (1u << b);
                }
            }
            return word;
        };

        uint8_t strb0 = 0;
        dut_->wdata  = pack(0, strb0);
        dut_->wstrb  = strb0;
        dut_->wlast  = (beats == 1) ? 1 : 0;
        dut_->wvalid = 1;

        for (uint32_t i = 0; i < beats; i++) {
            while (!dut_->wready) tick();
            tick();                // cloquear beat i

            if (i < beats - 1) {
                uint8_t strb = 0;
                dut_->wdata = pack(i + 1, strb);
                dut_->wstrb = strb;
                dut_->wlast = (i + 1 == beats - 1) ? 1 : 0;
            } else {
                dut_->wvalid = 0;
                dut_->wlast  = 0;
            }
        }

        // -- Write Response --
        dut_->bready = 1;
        while (!dut_->bvalid) tick();
        tick();
        bool ok = (dut_->bresp == 0);
        dut_->bready = 0;

        if (!ok) SC_REPORT_WARNING("RAM_RTL_SC", "BRESP != OKAY en escritura");
        return ok;
    }

    // =========================================================================
    // do_read  —  AR + R beats
    // =========================================================================
    bool do_read(uint32_t addr, uint8_t* data,
                 uint32_t bytes, uint32_t beats)
    {
        const uint8_t id = txn_id_++ & 0xF;

        // -- Read Address --
        dut_->arid    = id;
        dut_->araddr  = addr;
        dut_->arlen   = static_cast<uint8_t>(beats - 1);
        dut_->arsize  = 2;
        dut_->arburst = 0x1;
        dut_->arvalid = 1;

        while (!dut_->arready) tick();
        tick();                   
        dut_->arvalid = 0;

        // -- Read Data --
        // El RTL presenta el primer beat inmediatamente al capturar AR
        dut_->rready = 1;
        for (uint32_t i = 0; i < beats; i++) {
            while (!dut_->rvalid) tick();

            // Desempaquetar palabra de 32 bits al buffer de bytes
            uint32_t word = dut_->rdata;
            for (uint32_t b = 0; b < 4; b++) {
                uint32_t dst = i * 4 + b;
                if (dst < bytes) data[dst] = (word >> (b * 8)) & 0xFF;
            }

            if (dut_->rresp != 0) {
                SC_REPORT_WARNING("RAM_RTL_SC", "RRESP != OKAY en lectura");
                dut_->rready = 0;
                return false;
            }
            tick();
        }
        dut_->rready = 0;
        return true;
    }
};