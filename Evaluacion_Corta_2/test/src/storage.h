// =============================================================================
// storage.h  –  Módulo PersistentStorage
//
// Representa el almacenamiento en disco (carpeta local).
// El CPU accede directamente vía llamadas de método, NO por sockets TLM,
// porque el disco no vive en el espacio de direcciones del bus.
//
// =============================================================================
#pragma once

#include <systemc.h>
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>

// -----------------------------------------------------------------------------
// Constantes globales de imagen (1080p RAW RGB / Grayscale)
// Se definen aquí para que todos los módulos las compartan.
// -----------------------------------------------------------------------------
namespace ImageConfig {
    constexpr uint32_t WIDTH     = 1920;
    constexpr uint32_t HEIGHT    = 1080;
    constexpr uint32_t CH_RGB    = 3;
    constexpr size_t   RGB_SIZE  = (size_t)WIDTH * HEIGHT * CH_RGB;  // 6,220,800 B
    constexpr size_t   GRAY_SIZE = (size_t)WIDTH * HEIGHT;           // 2,073,600 B
}

// -----------------------------------------------------------------------------
// SC_MODULE: PersistentStorage
// -----------------------------------------------------------------------------
SC_MODULE(PersistentStorage) {

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    SC_CTOR(PersistentStorage) {
        SC_REPORT_INFO("Storage", "Módulo PersistentStorage creado");
    }

    // -------------------------------------------------------------------------
    // load_image
    // Lee un archivo RAW binario desde disco.
    //
    // Parámetro: path  – ruta al archivo (relativa al directorio de ejecución)
    // Retorna:   vector<uint8_t> con todos los bytes del archivo
    //
    // Termina la simulación (SC_REPORT_FATAL) si el archivo no se puede abrir.
    // Emite SC_REPORT_WARNING si el tamaño no coincide con 1080p RGB.
    // -------------------------------------------------------------------------
    std::vector<uint8_t> load_image(const std::string& path);

    // -------------------------------------------------------------------------
    // save_image
    // Escribe un vector de bytes a disco como archivo RAW binario.
    //
    // Parámetros:
    //   path – ruta destino (se crea o sobreescribe)
    //   data – bytes a guardar
    //
    // Termina la simulación (SC_REPORT_FATAL) si el archivo no se puede crear.
    // -------------------------------------------------------------------------
    void save_image(const std::string& path, const std::vector<uint8_t>& data);
};
