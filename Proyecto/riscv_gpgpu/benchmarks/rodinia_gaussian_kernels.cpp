// rodinia_gaussian_kernels.cpp - upstream Rodinia Gaussian kernels for the
// selective RISC-V integration path.
//
// This file keeps only the device kernels used by the benchmark smoke test:
//   - Fan1: computes the multiplier matrix column for a given pivot row
//   - Fan2: updates matrix A and vector B for the same pivot row

#include "rodinia_cuda_compat.h"

RodiniaThreadContext __gpgpu_thread_context = {
    {0, 0, 0},
    {0, 0, 0},
    {1, 1, 1},
    {1, 1, 1}
};

__global__ void Fan1(float *m_cuda, float *a_cuda, int Size, int t)
{
    if (threadIdx.x + blockIdx.x * blockDim.x >= Size - 1 - t) return;
    *(m_cuda + Size * (blockDim.x * blockIdx.x + threadIdx.x + t + 1) + t) =
        *(a_cuda + Size * (blockDim.x * blockIdx.x + threadIdx.x + t + 1) + t)
        / *(a_cuda + Size * t + t);
}

__global__ void Fan2(float *m_cuda, float *a_cuda, float *b_cuda, int Size, int j1, int t)
{
    if (threadIdx.x + blockIdx.x * blockDim.x >= Size - 1 - t) return;
    if (threadIdx.y + blockIdx.y * blockDim.y >= Size - t) return;

    int xidx = blockIdx.x * blockDim.x + threadIdx.x;
    int yidx = blockIdx.y * blockDim.y + threadIdx.y;

    a_cuda[Size * (xidx + 1 + t) + (yidx + t)] -=
        m_cuda[Size * (xidx + 1 + t) + t] * a_cuda[Size * t + (yidx + t)];

    if (yidx == 0) {
        b_cuda[xidx + 1 + t] -= m_cuda[Size * (xidx + 1 + t) + (yidx + t)] * b_cuda[t];
    }
}