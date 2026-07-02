// vector_add.cu – RISC-V GPGPU benchmark kernel
//
// Computes c[i] = a[i] + b[i] for i in [0, n).
// No C++ standard library headers — targets bare-metal RISC-V.

extern "C" {

// GPU kernel: one thread per element.
__global__ void vector_add(const int* __restrict__ a,
                           const int* __restrict__ b,
                           int*       __restrict__ c,
                           int n) {
    for (int i = 0; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
}

// Host-callable simulation entry: same semantics, no SIMT model.
void vec_add_host_sim(const int* a, const int* b, int* c, int n) {
    for (int i = 0; i < n; ++i) {
        c[i] = a[i] + b[i];
    }
}

} // extern "C"
