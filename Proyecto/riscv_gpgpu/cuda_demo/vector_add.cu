// Simple CUDA-style kernel: vector add
__global__ void vector_add(float *a, float *b, float *c, int n) {
    int idx = __global_thread_id();
    if (idx < n) {
        c[idx] = a[idx] + b[idx];
    }
}
