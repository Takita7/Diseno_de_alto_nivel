#!/bin/bash
#
# high_throughput.sh - High throughput scenario configuration
#
# Increases compute units and warps per CU for higher throughput
#

export GPGPU_NUM_COMPUTE_UNITS=8
export GPGPU_THREADS_PER_WARP=32
export GPGPU_MAX_WARPS_PER_CU=32
export GPGPU_SHARED_MEM_SIZE=98304  # 96KB
export GPGPU_L1_CACHE_SIZE=32768    # 32KB
export GPGPU_L2_CACHE_SIZE=524288   # 512KB
export GPGPU_CLOCK_FREQUENCY=150

echo "High throughput scenario configured"
