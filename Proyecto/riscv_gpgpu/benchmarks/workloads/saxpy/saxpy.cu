// saxpy.cu – RISC-V GPGPU benchmark kernel
//
// Computes z[i] = a * x[i] + y[i] (Single-precision A·X Plus Y).
// No C++ standard library headers — targets bare-metal RISC-V.

extern "C" {

// GPU kernel: one thread per element.
__global__ void saxpy(float        a,
                      const float* __restrict__ x,
                      const float* __restrict__ y,
                      float*       __restrict__ z,
                      int n) {
    for (int i = 0; i < n; ++i) {
        z[i] = a * x[i] + y[i];
    }
}

// Host-callable simulation entry.
void saxpy_host_sim(float a, const float* x, const float* y, float* z, int n) {
    for (int i = 0; i < n; ++i) {
        z[i] = a * x[i] + y[i];
    }
}

} // extern "C"
