#include "grayscale_accel.h"

#include <hls_stream.h>

// Represents one RGB pixel inside the HLS dataflow pipeline.
struct rgb_pixel_t {
    ap_uint<8> r;
    ap_uint<8> g;
    ap_uint<8> b;
};


// Read RGB pixels from external memory.
static void read_rgb_stage(
    const ap_uint<8>* input,
    hls::stream<rgb_pixel_t>& rgb_stream,
    uint32_t num_pixels)
{
    read_rgb_loop:
        for (uint32_t i = 0; i < num_pixels; ++i) {
            #pragma HLS PIPELINE II=1

                const uint32_t base_index = i * 3;

                rgb_pixel_t pixel;
                pixel.r = input[base_index];
                pixel.g = input[base_index + 1];
                pixel.b = input[base_index + 2];

                rgb_stream.write(pixel);
        }
}


// Convert RGB pixels to grayscale.
static void grayscale_stage(
    hls::stream<rgb_pixel_t>& rgb_stream,
    hls::stream<ap_uint<8>>& gray_stream,
    uint32_t num_pixels)
{
    grayscale_loop:
        for (uint32_t i = 0; i < num_pixels; ++i) {
            #pragma HLS PIPELINE II=1

                const rgb_pixel_t pixel = rgb_stream.read();
                const ap_uint<16> weighted_sum =
                    static_cast<ap_uint<16>>(77)  * pixel.r +
                    static_cast<ap_uint<16>>(150) * pixel.g +
                    static_cast<ap_uint<16>>(29)  * pixel.b;

                const ap_uint<8> gray = weighted_sum >> 8;

                gray_stream.write(gray);
        }
}


// Stage 3: Write grayscale pixels to external memory.
static void write_gray_stage(
    hls::stream<ap_uint<8>>& gray_stream,
    ap_uint<8>* output,
    uint32_t num_pixels)
{
    write_gray_loop:
        for (uint32_t i = 0; i < num_pixels; ++i) {
            #pragma HLS PIPELINE II=1

            output[i] = gray_stream.read();
        }
}

// Top-level HLS accelerator.
void grayscale_accel(
    const ap_uint<8>* input,
    ap_uint<8>* output,
    uint32_t num_pixels)
{
    #pragma HLS INTERFACE m_axi port=input \
        offset=slave \
        bundle=gmem_input \
        depth=6220800 \
        max_read_burst_length=64 \
        num_read_outstanding=16

    #pragma HLS INTERFACE m_axi port=output \
        offset=slave \
        bundle=gmem_output \
        depth=2073600 \
        max_write_burst_length=64 \
        num_write_outstanding=16

    #pragma HLS INTERFACE s_axilite port=input \
        bundle=control

    #pragma HLS INTERFACE s_axilite port=output \
        bundle=control

    #pragma HLS INTERFACE s_axilite port=num_pixels \
        bundle=control

    #pragma HLS INTERFACE s_axilite port=return \
        bundle=control

    #pragma HLS DATAFLOW

        hls::stream<rgb_pixel_t> rgb_stream("rgb_stream");
        hls::stream<ap_uint<8>> gray_stream("gray_stream");

    #pragma HLS STREAM variable=rgb_stream depth=64
    #pragma HLS STREAM variable=gray_stream depth=64

    read_rgb_stage(
        input,
        rgb_stream,
        num_pixels
    );

    grayscale_stage(
        rgb_stream,
        gray_stream,
        num_pixels
    );

    write_gray_stage(
        gray_stream,
        output,
        num_pixels
    );
}