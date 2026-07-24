#ifndef GRAYSCALE_ACCEL_H
#define GRAYSCALE_ACCEL_H

#include <ap_int.h>
#include <stdint.h>

void grayscale_accel(
    const ap_uint<8>* input,
    ap_uint<8>* output,
    uint32_t num_pixels
);

#endif // GRAYSCALE_ACCEL_H