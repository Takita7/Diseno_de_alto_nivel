#ifndef RISCV_GPGPU_BENCHMARKS_RODINIA_CUDA_COMPAT_H
#define RISCV_GPGPU_BENCHMARKS_RODINIA_CUDA_COMPAT_H

struct dim3 {
    int x;
    int y;
    int z;
};

struct RodiniaThreadContext {
    dim3 threadIdx;
    dim3 blockIdx;
    dim3 blockDim;
    dim3 gridDim;
};

extern RodiniaThreadContext __gpgpu_thread_context;

#define __global__
#define __device__
#define __host__
#define __shared__
#define __constant__
#define __syncthreads() do { } while (0)

#define blockIdx (__gpgpu_thread_context.blockIdx)
#define threadIdx (__gpgpu_thread_context.threadIdx)
#define blockDim (__gpgpu_thread_context.blockDim)
#define gridDim (__gpgpu_thread_context.gridDim)

#endif // RISCV_GPGPU_BENCHMARKS_RODINIA_CUDA_COMPAT_H