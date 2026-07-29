// =============================================================================
// sim_main.cpp  -  Testbench C++ standalone para Verilator
//
// Verifica ram_axi4.sv con los mismos tests que tb_ram_basic.sv (UVM)
// antes de integrarlo con SystemC
//
// Los drivers AXI4 de este archivo son la base de ram_rtl_sc.h.
//
// Compilar y correr:
//   make run_vl
//
// API Verilator 5.x:
//   - VerilatedContext maneja el tiempo de simulacion
//   - dut->signal accede directamente a puertos del DUT
//   - dut->eval() propaga cambios combinacionales
//   - tick() avanza un ciclo completo de clock
// =============================================================================

#include "Vram_axi4.h"
#include "verilated.h"
#include "verilated_vcd_c.h"

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>

// ---- Globales ---------------------------------------------------------------
static VerilatedContext* ctx = nullptr;
static Vram_axi4*        dut = nullptr;
static VerilatedVcdC*    vcd = nullptr;

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

// ---- Colores ----------------------------------------------------------------
#define GRN "\033[32m"
#define RED "\033[31m"
#define CYN "\033[36m"
#define RST "\033[0m"

// =============================================================================
// tick() - avanza un ciclo completo de clock (posedge + negedge)
//
// Patron de timing para Verilator con RTL registrado:
//   - Inputs se manejan ANTES del tick()
//   - Outputs se leen DESPUES del tick() (reflejan el estado post-posedge)
// =============================================================================
static void tick()
{
    // Posedge
    dut->clk = 1;
    dut->eval();
    if (vcd) vcd->dump(ctx->time());
    ctx->timeInc(5);

    // Negedge
    dut->clk = 0;
    dut->eval();
    if (vcd) vcd->dump(ctx->time());
    ctx->timeInc(5);
}

// =============================================================================
// reset() - aplica reset activo-bajo por N ciclos
// =============================================================================
static void reset(int cycles = 5)
{
    dut->rst = 0;
    for (int i = 0; i < cycles; i++) tick();
    dut->rst = 1;
    tick();
    tick();
}

// =============================================================================
// CHECK macro
// =============================================================================
#define CHECK(desc, cond) do {                                   \
    tests_run++;                                                 \
    if (cond) {                                                  \
        std::cout << "  " GRN "[PASS]" RST " " << (desc) << "\n"; \
        tests_passed++;                                          \
    } else {                                                     \
        std::cout << "  " RED "[FAIL]" RST " " << (desc) << "\n"; \
        tests_failed++;                                          \
    }                                                            \
} while(0)

// =============================================================================
// axi4_write()
//
// Patron pipelined validado en UVM:
//   1. AW handshake
//   2. Pre-drive beat 0, luego actualizar wdata en el mismo ciclo del handshake
//   3. Esperar B response
//
// Timing Verilator:
//   while(!ready) tick()  - avanza hasta que ready=1 (post-posedge)
//   tick()                - cloquea el handshake
//   [actualizar datos]    - antes del proximo tick
// =============================================================================
static void axi4_write(uint32_t               addr,
                       const std::vector<uint32_t>& data,
                       const std::vector<uint8_t>&  strb,
                       uint8_t id = 1)
{
    int beats = (int)data.size();

    // -- Write Address --------------------------------------------------------
    dut->awid    = id & 0xF;
    dut->awaddr  = addr;
    dut->awlen   = (uint8_t)(beats - 1);
    dut->awsize  = 2;       // 2^2 = 4 bytes por beat
    dut->awburst = 0x1;     // INCR
    dut->awvalid = 1;

    while (!dut->awready) tick();
    tick();   // cloquear handshake: RTL ve awvalid=1 && awready=1
    dut->awvalid = 0;

    // -- Write Data (pipelined) -----------------------------------------------
    dut->wdata  = data[0];
    dut->wstrb  = strb[0] & 0xF;
    dut->wlast  = (beats == 1) ? 1 : 0;
    dut->wvalid = 1;

    for (int i = 0; i < beats; i++) {
        while (!dut->wready) tick();
        tick();   // cloquear beat i

        if (i < beats - 1) {
            // Pre-drive siguiente beat en el mismo ciclo del handshake
            dut->wdata = data[i + 1];
            dut->wstrb = strb[i + 1] & 0xF;
            dut->wlast = (i + 1 == beats - 1) ? 1 : 0;
        } else {
            dut->wvalid = 0;
            dut->wlast  = 0;
        }
    }

    // -- Write Response -------------------------------------------------------
    dut->bready = 1;
    while (!dut->bvalid) tick();
    tick();   // cloquear B handshake
    dut->bready = 0;
}

// =============================================================================
// axi4_read()
//
// El RTL presenta el primer beat inmediatamente al capturar AR (en RS_IDLE).
// Despues de la AR handshake tick(), rvalid=1 y rdata=beat0 son visibles.
// =============================================================================
static std::vector<uint32_t> axi4_read(uint32_t addr, int beats, uint8_t id = 2)
{
    std::vector<uint32_t> rd(beats, 0);

    // -- Read Address ---------------------------------------------------------
    dut->arid    = id & 0xF;
    dut->araddr  = addr;
    dut->arlen   = (uint8_t)(beats - 1);
    dut->arsize  = 2;
    dut->arburst = 0x1;
    dut->arvalid = 1;

    while (!dut->arready) tick();
    tick();   // cloquear AR handshake: RTL lanza primer beat
    dut->arvalid = 0;

    // -- Read Data ------------------------------------------------------------
    dut->rready = 1;
    for (int i = 0; i < beats; i++) {
        while (!dut->rvalid) tick();
        rd[i] = dut->rdata;   // capturar dato (post-posedge, estable)
        tick();                // cloquear beat i: RTL prepara siguiente
    }
    dut->rready = 0;

    return rd;
}

// =============================================================================
// TESTS
// =============================================================================

static void test1_single_wr_rd()
{
    std::cout << "\n-- TEST 1: Single write / single read --\n";
    axi4_write(0x001000, {0xDEADBEEF}, {0xF});
    auto rd = axi4_read(0x001000, 1);
    CHECK("rd[0] == 0xDEADBEEF", rd[0] == 0xDEADBEEF);
}

static void test2_burst8()
{
    std::cout << "\n-- TEST 2: Burst 8 beats --\n";
    std::vector<uint32_t> wr(8);
    std::vector<uint8_t>  st(8, 0xF);
    for (int i = 0; i < 8; i++) wr[i] = 0xA0000000u + i;

    axi4_write(0x002000, wr, st);
    auto rd = axi4_read(0x002000, 8);

    for (int i = 0; i < 8; i++)
        CHECK("burst rd[" + std::to_string(i) + "] == 0xA000_000" + std::to_string(i),
              rd[i] == wr[i]);
}

static void test3_strb()
{
    std::cout << "\n-- TEST 3: Byte enables parciales --\n";
    // Escribir 0xFFFF_FFFF completo
    axi4_write(0x003000, {0xFFFFFFFFu}, {0xF});
    // Reescribir solo bytes [3:2] con strb=0xC
    axi4_write(0x003000, {0xCAFE0000u}, {0xC});
    auto rd = axi4_read(0x003000, 1);
    CHECK("strb parcial == 0xCAFEFFFF", rd[0] == 0xCAFEFFFFu);
}

static void test4_burst32()
{
    std::cout << "\n-- TEST 4: Burst 32 beats --\n";
    std::vector<uint32_t> wr(32);
    std::vector<uint8_t>  st(32, 0xF);
    for (int i = 0; i < 32; i++) wr[i] = 0xC0DE0000u + i;

    axi4_write(0x004000, wr, st);
    auto rd = axi4_read(0x004000, 32);

    bool all_ok = true;
    for (int i = 0; i < 32; i++) {
        if (rd[i] != wr[i]) {
            std::cout << "  " RED "[FAIL]" RST " beat " << i
                      << " exp=0x" << std::hex << wr[i]
                      << " got=0x" << rd[i] << std::dec << "\n";
            all_ok = false;
            tests_failed++;
            tests_run++;
        }
    }
    if (all_ok) {
        std::cout << "  " GRN "[PASS]" RST " burst32: 32 beats correctos\n";
        tests_passed++;
        tests_run++;
    }
}

static void test5_bready_delay()
{
    std::cout << "\n-- TEST 5: bready demorado (5 ciclos) --\n";

    // Write Address
    dut->awid = 4; dut->awaddr = 0x005000;
    dut->awlen = 0; dut->awsize = 2; dut->awburst = 1;
    dut->awvalid = 1;
    while (!dut->awready) tick();
    tick();
    dut->awvalid = 0;

    // Write Data
    dut->wdata = 0xBEEF1234u; dut->wstrb = 0xF;
    dut->wlast = 1; dut->wvalid = 1;
    while (!dut->wready) tick();
    tick();
    dut->wvalid = 0; dut->wlast = 0;

    // Demorar bready 5 ciclos
    for (int i = 0; i < 5; i++) tick();
    dut->bready = 1;
    while (!dut->bvalid) tick();
    tick();
    dut->bready = 0;

    auto rd = axi4_read(0x005000, 1);
    CHECK("bready demorado: rd == 0xBEEF1234", rd[0] == 0xBEEF1234u);
}

// =============================================================================
// main
// =============================================================================
int main(int argc, char** argv)
{
    // ---- Setup Verilator ----------------------------------------------------
    ctx = new VerilatedContext;
    ctx->commandArgs(argc, argv);
    ctx->traceEverOn(true);

    dut = new Vram_axi4{ctx};

    // VCD opcional: ./sim_verilator +trace
    if (ctx->commandArgsPlusMatch("trace")) {
        vcd = new VerilatedVcdC;
        dut->trace(vcd, 99);
        vcd->open("sim_main.vcd");
        std::cout << CYN "[Trace] Guardando waveforms en sim_main.vcd" RST "\n";
    }

    // ---- Inicializar senales ------------------------------------------------
    dut->clk    = 0; dut->rst = 0;
    dut->awvalid = 0; dut->awid    = 0;
    dut->awaddr  = 0; dut->awlen   = 0;
    dut->awsize  = 0; dut->awburst = 0;
    dut->wvalid  = 0; dut->wdata   = 0;
    dut->wstrb   = 0; dut->wlast   = 0;
    dut->bready  = 0;
    dut->arvalid = 0; dut->arid    = 0;
    dut->araddr  = 0; dut->arlen   = 0;
    dut->arsize  = 0; dut->arburst = 0;
    dut->rready  = 0;
    dut->eval();

    std::cout << CYN "\n=== sim_main: Verilator C++ testbench ===" RST "\n";

    // ---- Reset --------------------------------------------------------------
    reset();

    // ---- Correr tests -------------------------------------------------------
    test1_single_wr_rd();
    test2_burst8();
    test3_strb();
    test4_burst32();
    test5_bready_delay();

    // ---- Resumen ------------------------------------------------------------
    std::cout << "\n=== RESUMEN ===\n";
    std::cout << "  Tests corridos : " << tests_run    << "\n";
    std::cout << "  " GRN "PASSED" RST "         : " << tests_passed << "\n";
    std::cout << "  " RED "FAILED" RST "         : " << tests_failed << "\n";
    if (tests_failed == 0)
        std::cout << GRN "  [OK] Todos los tests pasaron\n" RST;
    else
        std::cout << RED "  [!!] " << tests_failed << " test(s) fallaron\n" RST;

    // ---- Cleanup ------------------------------------------------------------
    if (vcd) { vcd->close(); delete vcd; }
    dut->final();
    delete dut;
    delete ctx;

    return (tests_failed == 0) ? 0 : 1;
}