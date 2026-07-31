// =============================================================================
// axi4_if.h  –  Estructuras de los 5 canales AXI4 Full
//
// Define los payloads de cada canal del protocolo AXI4 Full.
// Usado por TLM_to_AXI4_Bridge (initiator) y RAM_AXI4_Model (target).
//
// Canales:
//   AW  –  Write Address
//   W   –  Write Data  (un beat por struct)
//   B   –  Write Response
//   AR  –  Read Address
//   R   –  Read Data   (un beat por struct)
//
// Data bus: 32 bits (4 bytes por beat, awsize/arsize = 2 -> 2^2 = 4)
// =============================================================================
#pragma once

#include <cstdint>
#include <array>
#include <ostream>

// ── Parámetros del bus ────────────────────────────────────────────────────────
static constexpr uint32_t AXI4_DATA_BYTES = 4;    // ancho del bus de datos (bytes)
static constexpr uint32_t AXI4_MAX_BURST  = 256;  // beats máximos por burst (spec AXI4)

// ── Códigos BRESP / RRESP ─────────────────────────────────────────────────────
static constexpr uint8_t AXI4_RESP_OKAY   = 0b00; // transacción exitosa
static constexpr uint8_t AXI4_RESP_SLVERR = 0b10; // error del esclavo (ej. out-of-range)

// ── Tipos de burst (AWBURST / ARBURST) ───────────────────────────────────────
static constexpr uint8_t AXI4_BURST_FIXED = 0b00; // misma dirección en cada beat
static constexpr uint8_t AXI4_BURST_INCR  = 0b01; // dirección incrementa por beat
static constexpr uint8_t AXI4_BURST_WRAP  = 0b10; // burst con wrap (no usado aquí)

// =============================================================================
// AW  –  Write Address Channel
// =============================================================================
struct AXI4_AW {
    uint32_t awid    = 0;
    uint64_t awaddr  = 0;
    uint8_t  awlen   = 0;                  // beats en el burst = awlen + 1
    uint8_t  awsize  = 2;                  // bytes/beat = 2^awsize  (2 -> 4 bytes)
    uint8_t  awburst = AXI4_BURST_INCR;

    friend std::ostream& operator<<(std::ostream& os, const AXI4_AW& v) {
        return os << "AW{id=" << v.awid
                  << " addr=0x" << std::hex << v.awaddr << std::dec
                  << " len=" << (int)v.awlen
                  << " burst=" << (int)v.awburst << "}";
    }
};

// =============================================================================
// W  –  Write Data Channel (un beat)
// =============================================================================
struct AXI4_W {
    std::array<uint8_t, AXI4_DATA_BYTES> wdata = {};
    uint8_t wstrb = 0xF;    // byte-enable: bit i=1 -> byte i es válido
    bool    wlast = false;  // true en el último beat del burst

    friend std::ostream& operator<<(std::ostream& os, const AXI4_W& v) {
        os << "W{strb=0x" << std::hex << (int)v.wstrb << " data=[";
        for (auto b : v.wdata) os << (int)b << " ";
        return os << "] last=" << v.wlast << "}" << std::dec;
    }
};

// =============================================================================
// B  –  Write Response Channel
// =============================================================================
struct AXI4_B {
    uint32_t bid   = 0;
    uint8_t  bresp = AXI4_RESP_OKAY;

    friend std::ostream& operator<<(std::ostream& os, const AXI4_B& v) {
        return os << "B{id=" << v.bid
                  << " resp=" << (int)v.bresp << "}";
    }
};

// =============================================================================
// AR  –  Read Address Channel
// =============================================================================
struct AXI4_AR {
    uint32_t arid    = 0;
    uint64_t araddr  = 0;
    uint8_t  arlen   = 0;
    uint8_t  arsize  = 2;
    uint8_t  arburst = AXI4_BURST_INCR;

    friend std::ostream& operator<<(std::ostream& os, const AXI4_AR& v) {
        return os << "AR{id=" << v.arid
                  << " addr=0x" << std::hex << v.araddr << std::dec
                  << " len=" << (int)v.arlen
                  << " burst=" << (int)v.arburst << "}";
    }
};

// =============================================================================
// R  –  Read Data Channel (un beat)
// =============================================================================
struct AXI4_R {
    uint32_t rid   = 0;
    std::array<uint8_t, AXI4_DATA_BYTES> rdata = {};
    uint8_t  rresp = AXI4_RESP_OKAY;
    bool     rlast = false;  // true en el último beat del burst

    friend std::ostream& operator<<(std::ostream& os, const AXI4_R& v) {
        os << "R{id=" << v.rid << " data=[";
        for (auto b : v.rdata) os << std::hex << (int)b << " ";
        return os << "] resp=" << (int)v.rresp
                  << " last=" << v.rlast << "}" << std::dec;
    }
};