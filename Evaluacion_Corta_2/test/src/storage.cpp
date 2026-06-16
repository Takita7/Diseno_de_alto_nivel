// =============================================================================
// storage.cpp  –  Implementación de PersistentStorage
// =============================================================================
#include "storage.h"
#include <sstream>
#include <iostream>

// -----------------------------------------------------------------------------
// load_image
// -----------------------------------------------------------------------------
std::vector<uint8_t> PersistentStorage::load_image(const std::string& path) {

    // Abrir en modo binario, posicionar al final para medir tamaño
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        std::string msg = "No se puede abrir archivo: " + path;
        SC_REPORT_FATAL("Storage", msg.c_str());
        return {};  // nunca se alcanza, SC_REPORT_FATAL aborta la simulación
    }

    size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    // Verificar tamaño esperado para 1080p RGB
    if (file_size != ImageConfig::RGB_SIZE) {
        std::ostringstream oss;
        oss << "Tamaño inesperado: " << file_size
            << " B (esperado " << ImageConfig::RGB_SIZE << " B para 1080p RGB)";
        SC_REPORT_WARNING("Storage", oss.str().c_str());
    }

    // Leer contenido completo
    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
        SC_REPORT_FATAL("Storage", ("Error al leer: " + path).c_str());
        return {};
    }

    // Log con tiempo de simulación
    std::ostringstream info;
    info << "load_image | " << file_size << " bytes"
         << " | t=" << sc_time_stamp().to_string()
         << " | \"" << path << "\"";
    SC_REPORT_INFO("Storage", info.str().c_str());

    return buffer;
}

// -----------------------------------------------------------------------------
// save_image
// -----------------------------------------------------------------------------
void PersistentStorage::save_image(const std::string& path,
                                    const std::vector<uint8_t>& data) {

    std::ofstream file(path, std::ios::binary);

    if (!file.is_open()) {
        SC_REPORT_FATAL("Storage", ("No se puede crear: " + path).c_str());
        return;
    }

    file.write(reinterpret_cast<const char*>(data.data()), data.size());

    if (!file) {
        SC_REPORT_FATAL("Storage", ("Error al escribir: " + path).c_str());
        return;
    }

    std::ostringstream info;
    info << "save_image | " << data.size() << " bytes"
         << " | t=" << sc_time_stamp().to_string()
         << " | \"" << path << "\"";
    SC_REPORT_INFO("Storage", info.str().c_str());
}
