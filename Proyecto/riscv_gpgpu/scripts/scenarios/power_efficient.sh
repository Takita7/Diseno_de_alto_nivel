#!/bin/bash
#
# power_efficient.sh - Power-efficient scenario configuration
#
# Reduces compute units and clock frequency for lower power
#

export GPGPU_NUM_COMPUTE_UNITS=2
export GPGPU_THREADS_PER_WARP=32
export GPGPU_MAX_WARPS_PER_CU=8
export GPGPU_SHARED_MEM_SIZE=24576  # 24KB
export GPGPU_L1_CACHE_SIZE=8192     # 8KB
export GPGPU_L2_CACHE_SIZE=131072   # 128KB
export GPGPU_CLOCK_FREQUENCY=50

echo "Power-efficient scenario configured"
