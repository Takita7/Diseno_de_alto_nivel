#!/bin/bash
#
# baseline.sh - Baseline scenario configuration
#
# Defines configuration parameters for baseline simulation
#

export GPGPU_NUM_COMPUTE_UNITS=4
export GPGPU_THREADS_PER_WARP=32
export GPGPU_MAX_WARPS_PER_CU=16
export GPGPU_SHARED_MEM_SIZE=49152
export GPGPU_L1_CACHE_SIZE=16384
export GPGPU_L2_CACHE_SIZE=262144
export GPGPU_CLOCK_FREQUENCY=100

echo "Baseline scenario configured"
