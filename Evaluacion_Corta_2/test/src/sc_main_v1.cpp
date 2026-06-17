// =============================================================================
// sc_main.cpp  –  Stage 1: Prueba del módulo PersistentStorage
//
// Qué verifica esta etapa:
//   1. Carga images/input.raw y verifica tamaño (6,220,800 bytes)
//   2. Inspecciona primer y último píxel
//   3. Guarda una copia y la compara byte-a-byte con el original
//
// Antes de correr: generar la imagen con  python3 scripts/gen_raw.py
// =============================================================================
#include <systemc.h>
#include "storage.h"

#include <iostream>
#include <fstream>
#include <cassert>
#include <iomanip>

// Colores para la consola (opcional, ignora si no funciona en tu terminal)
#define CLR_OK   "\033[32m"
#define CLR_ERR  "\033[31m"
#define CLR_INFO "\033[36m"
#define CLR_RST  "\033[0m"

// ──────────────────────────────────────────────────────────────────────────────
int sc_main(int /*argc*/, char* /*argv*/[]) {

    std::cout << CLR_INFO
              << "\n=== TLM Image Processor – Stage 1: PersistentStorage ==="
              << CLR_RST << "\n";

    // ── Verificar que existe el archivo de entrada ────────────────────────────
    const std::string INPUT_PATH  = "images/input.raw";
    const std::string OUTPUT_PATH = "images/output_copy.raw";

    {
        std::ifstream check(INPUT_PATH);
        if (!check.is_open()) {
            std::cerr << CLR_ERR
                      << "\n[ERROR] No se encontró: " << INPUT_PATH
                      << "\nEjecutar primero:\n"
                      << "    python3 scripts/gen_raw.py\n"
                      << CLR_RST << "\n";
            return 1;
        }
    }

    // ── Instanciar módulo de almacenamiento ───────────────────────────────────
    PersistentStorage storage("storage");

    // sc_start(SC_ZERO_TIME) inicializa la simulación sin avanzar el reloj.
    // Es necesario para que sc_time_stamp() devuelva 0 ns (y no un valor
    // no-inicializado). En stages siguientes, los SC_THREADs arrancarán aquí.
    sc_start(SC_ZERO_TIME);

    // ── TEST 1: cargar imagen ─────────────────────────────────────────────────
    std::cout << "\n[TEST 1] Cargando imagen desde disco...\n";

    std::vector<uint8_t> img = storage.load_image(INPUT_PATH);

    assert(!img.empty() && "La imagen cargada está vacía");
    assert(img.size() == ImageConfig::RGB_SIZE && "Tamaño incorrecto");

    std::cout << CLR_OK << "  [OK] " << CLR_RST
              << img.size() << " bytes cargados ("
              << ImageConfig::WIDTH << "x" << ImageConfig::HEIGHT
              << " x " << ImageConfig::CH_RGB << " canales)\n";

    // ── TEST 2: inspeccionar píxeles ──────────────────────────────────────────
    std::cout << "\n[TEST 2] Inspeccionando píxeles...\n";

    auto print_pixel = [&](const std::string& label, size_t offset) {
        std::cout << "  " << std::setw(20) << std::left << label
                  << " R=" << std::setw(4) << (int)img[offset]
                  << " G=" << std::setw(4) << (int)img[offset + 1]
                  << " B=" << std::setw(4) << (int)img[offset + 2]
                  << "\n";
    };

    print_pixel("Pixel [0,0]:",    0);
    print_pixel("Pixel [0,959]:",  (0 * ImageConfig::WIDTH + 959) * 3);
    print_pixel("Pixel [539,0]:",  (539 * ImageConfig::WIDTH + 0) * 3);
    print_pixel("Pixel [1079,1919]:", (img.size() - 3));

    std::cout << CLR_OK << "  [OK] " << CLR_RST << "Contenido accesible\n";

    // ── TEST 3: guardar copia y comparar ──────────────────────────────────────
    std::cout << "\n[TEST 3] Guardando copia en: " << OUTPUT_PATH << " ...\n";

    storage.save_image(OUTPUT_PATH, img);

    std::cout << "  Verificando copia byte-a-byte...\n";
    std::vector<uint8_t> copy = storage.load_image(OUTPUT_PATH);

    assert(copy.size() == img.size() && "Tamaño de copia incorrecto");
    assert(copy == img && "La copia no coincide con el original");

    std::cout << CLR_OK << "  [OK] " << CLR_RST
              << "Copia coincide con original (" << copy.size() << " bytes)\n";

    // ── Avanzar tiempo y cerrar ───────────────────────────────────────────────
    sc_start(sc_time(10, SC_NS));

    std::cout << CLR_INFO
              << "\n=== Stage 1 COMPLETO | t=" << sc_time_stamp() << " ==="
              << CLR_RST << "\n\n";

    return 0;
}
