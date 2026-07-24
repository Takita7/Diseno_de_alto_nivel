#include "grayscale_accel.h"

#include <ap_int.h>

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Image configuration: RAW RGB, 1920 x 1080
static constexpr uint32_t IMAGE_WIDTH  = 1920;
static constexpr uint32_t IMAGE_HEIGHT = 1080;
static constexpr uint32_t NUM_PIXELS =
    IMAGE_WIDTH * IMAGE_HEIGHT;

static constexpr std::size_t RGB_SIZE =
    static_cast<std::size_t>(NUM_PIXELS) * 3U;

static constexpr std::size_t GRAY_SIZE =
    static_cast<std::size_t>(NUM_PIXELS);

// Read a binary RAW file
static bool read_binary_file(
    const std::string& path,
    std::vector<uint8_t>& data)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open()) {
        std::cerr << "[ERROR] Cannot open input file: "
                  << path << '\n';
        return false;
    }

    const std::streamsize file_size = file.tellg();

    if (file_size < 0) {
        std::cerr << "[ERROR] Cannot determine file size: "
                  << path << '\n';
        return false;
    }

    file.seekg(0, std::ios::beg);

    data.resize(static_cast<std::size_t>(file_size));

    if (!file.read(
            reinterpret_cast<char*>(data.data()),
            file_size)) {
        std::cerr << "[ERROR] Cannot read input file: "
                  << path << '\n';
        return false;
    }

    return true;
}


// Write a binary RAW file
static bool write_binary_file(
    const std::string& path,
    const std::vector<uint8_t>& data)
{
    std::ofstream file(path, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "[ERROR] Cannot create output file: "
                  << path << '\n';
        return false;
    }

    file.write(
        reinterpret_cast<const char*>(data.data()),
        static_cast<std::streamsize>(data.size()));

    if (!file) {
        std::cerr << "[ERROR] Cannot write output file: "
                  << path << '\n';
        return false;
    }

    return true;
}


// Software reference model
static uint8_t grayscale_reference(
    uint8_t red,
    uint8_t green,
    uint8_t blue)
{
    const uint32_t weighted_sum =
        77U  * static_cast<uint32_t>(red) +
        150U * static_cast<uint32_t>(green) +
        29U  * static_cast<uint32_t>(blue);

    return static_cast<uint8_t>(weighted_sum >> 8);
}

// -----------------------------------------------------------------------------
// Testbench entry point
//
// Optional arguments:
//   argv[1] = input RAW file
//   argv[2] = output RAW file
//
// Default paths assume Vitis HLS is launched from Evaluacion_Corta_3/HLS.
// -----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    const std::string input_path =
        (argc >= 2) ? argv[1] : "data/input.raw";

    const std::string output_path =
        (argc >= 3) ? argv[2] : "data/output_hls.raw";

    std::cout << "========================================\n";
    std::cout << " HLS RGB-to-Grayscale Testbench\n";
    std::cout << "========================================\n";
    std::cout << "Input:       " << input_path << '\n';
    std::cout << "Output:      " << output_path << '\n';
    std::cout << "Resolution:  "
              << IMAGE_WIDTH << " x " << IMAGE_HEIGHT << '\n';
    std::cout << "Pixels:      " << NUM_PIXELS << '\n';

    // Load the RGB RAW image
    std::vector<uint8_t> input_file;

    if (!read_binary_file(input_path, input_file)) {
        return 1;
    }

    std::cout << "Input bytes: " << input_file.size() << '\n';

    if (input_file.size() != RGB_SIZE) {
        std::cerr
            << "[ERROR] Invalid input size.\n"
            << "Expected: " << RGB_SIZE
            << " bytes for a 1920x1080 RGB image.\n"
            << "Received: " << input_file.size()
            << " bytes.\n";

        return 1;
    }

    // Convert the input into Vitis HLS data types
    std::vector<ap_uint<8>> accelerator_input(RGB_SIZE);
    std::vector<ap_uint<8>> accelerator_output(GRAY_SIZE);

    for (std::size_t i = 0; i < RGB_SIZE; ++i) {
        accelerator_input[i] = input_file[i];
    }

    // Initialize the output to make incomplete writes easier to detect.
    for (std::size_t i = 0; i < GRAY_SIZE; ++i) {
        accelerator_output[i] = 0;
    }

    // Execute the HLS accelerator
    std::cout << "\n[INFO] Running grayscale_accel...\n";

    grayscale_accel(
        accelerator_input.data(),
        accelerator_output.data(),
        NUM_PIXELS);

    std::cout << "[INFO] Accelerator execution completed.\n";

    // Compare hardware-model output against software reference
    std::vector<uint8_t> output_file(GRAY_SIZE);

    uint32_t mismatch_count = 0;
    static constexpr uint32_t MAX_PRINTED_MISMATCHES = 10;

    for (uint32_t pixel_index = 0;
         pixel_index < NUM_PIXELS;
         ++pixel_index) {

        const std::size_t rgb_index =
            static_cast<std::size_t>(pixel_index) * 3U;

        const uint8_t red   = input_file[rgb_index];
        const uint8_t green = input_file[rgb_index + 1U];
        const uint8_t blue  = input_file[rgb_index + 2U];

        const uint8_t expected =
            grayscale_reference(red, green, blue);

        const uint8_t obtained =
            static_cast<uint8_t>(
                accelerator_output[pixel_index].to_uint());

        output_file[pixel_index] = obtained;

        if (obtained != expected) {
            if (mismatch_count < MAX_PRINTED_MISMATCHES) {
                std::cerr
                    << "[MISMATCH] Pixel " << pixel_index
                    << " RGB=("
                    << static_cast<unsigned>(red) << ", "
                    << static_cast<unsigned>(green) << ", "
                    << static_cast<unsigned>(blue) << ")"
                    << " expected="
                    << static_cast<unsigned>(expected)
                    << " obtained="
                    << static_cast<unsigned>(obtained)
                    << '\n';
            }

            ++mismatch_count;
        }
    }

    // Save the HLS-generated grayscale image
    if (!write_binary_file(output_path, output_file)) {
        return 1;
    }

    std::cout << "\nOutput bytes: " << output_file.size() << '\n';
    std::cout << "Output saved: " << output_path << '\n';

    // Final test result
    if (mismatch_count != 0) {
        std::cerr
            << "\n========================================\n"
            << " TEST FAILED\n"
            << " Mismatches: " << mismatch_count << '\n'
            << "========================================\n";

        return 1;
    }

    std::cout
        << "\n========================================\n"
        << " TEST PASSED\n"
        << " All " << NUM_PIXELS
        << " pixels match the reference model.\n"
        << "========================================\n";

    return 0;
}