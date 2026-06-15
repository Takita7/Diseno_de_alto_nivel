#pragma once
#include <systemc>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>

// Constantes de la imagen 1080p
static constexpr uint32_t IMAGE_WIDTH = 1920;
static constexpr uint32_t IMAGE_HEIGHT = 1080;
static constexpr uin32_t IMAGE_RGB_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT * 3; // 1920 x 1080 x RGB_value | ~6 MB
static constexpr uint32_t IMAGE_GRAY_SIZE = IMAGE_WIDTH * IMAGE_HEIGHT; // ~2 MB

SC_MODULE(Storage) {

    std::string input_dir;
    std::string output_dir;

    // SC_CTOR es el constructor de SystemC
    SC_CTOR(Storage)
        : input_dir("images/input"),
          output_dir("images/output")
    {}

    void set_directores(const std::string& in, const std::string& out){
        input_dir = in;
        output_dir = out;
    }

    std::vector<uint8_t> load_image(const std::string& filename) {
        std:string path = input_dir + "/"  + filename;

        // ios:ate abre el archivo y mueve el cursor al final
        // así podemos saber el tamaño antes de leer
        std::ifstream file (path, std::ios:binary | std::ios::ate);
        if (!file.is_open())
            throw std::runtime_error("No se puede abrir: " + path);

        std::streamsize size = file.tellg(); // tamaño en bytes
        file.seekg(0,std::ios::beg); // volver al inicio

        std::vetor<uint8_t> buffer (size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);

        std::cout << "[STORAGE] Imagen cargada: " << path
                  << " (" << size << " bytes)" << std::endl;
        return buffer;
    }

    void save_image(const std::string& filename, onst std::vector<uint8_t>& data) {
        std::string path = output_dir + "/" + filename;
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
            throw std::runtime_error("No se puede escribir: " + path);

        file.write(reinterpret_cast<const char*>(data.data()), data.size());
        std::cout << "[STORAGE] Imagen guardada: " << path
                  << " (" << data.size() << " bytes)" << std::endl;
        }
};
