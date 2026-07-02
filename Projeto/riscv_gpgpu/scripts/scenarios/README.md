# Scenarios

This directory contains simulation scenario scripts for evaluating different configurations.

## Available Scenarios

### baseline.sh
Standard baseline configuration:
- 4 compute units
- 32 threads per warp
- 16 warps per CU (512 threads per CU)
- 48KB shared memory
- 16KB L1 cache
- 256KB L2 cache

### high_throughput.sh
Configuration optimized for throughput:
- 8 compute units
- 32 threads per warp
- 32 warps per CU (1024 threads per CU)
- 96KB shared memory
- 32KB L1 cache
- 512KB L2 cache
- Higher clock frequency

### power_efficient.sh
Configuration optimized for power efficiency:
- 2 compute units
- 32 threads per warp
- 8 warps per CU (256 threads per CU)
- 24KB shared memory
- 8KB L1 cache
- 128KB L2 cache
- Lower clock frequency

## Running Scenarios

```bash
# Run baseline scenario
./scripts/run_systemc_sim.sh --scenario baseline

# Run high throughput scenario
./scripts/run_systemc_sim.sh --scenario high_throughput

# Run with custom configuration
./scripts/run_systemc_sim.sh --config custom_config.yaml
```

## Creating New Scenarios

1. Create a new shell script in this directory (e.g., `custom_scenario.sh`)
2. Define environment variables for configuration parameters
3. Run: `./scripts/run_systemc_sim.sh --scenario custom_scenario`

## Configuration Parameters

- `GPGPU_NUM_COMPUTE_UNITS`: Number of compute units
- `GPGPU_THREADS_PER_WARP`: Threads per SIMT warp
- `GPGPU_MAX_WARPS_PER_CU`: Maximum warps per compute unit
- `GPGPU_SHARED_MEM_SIZE`: Shared memory size in bytes
- `GPGPU_L1_CACHE_SIZE`: L1 cache size in bytes
- `GPGPU_L2_CACHE_SIZE`: L2 cache size in bytes
- `GPGPU_CLOCK_FREQUENCY`: Clock frequency in MHz

## Measurement Collection

Results from each scenario are automatically saved to:
- `results/simulation/` - Simulation logs
- `results/simulation/scenario_name/` - Scenario-specific results

Results include:
- Execution cycles
- Instruction count
- Cache hit/miss statistics
- Divergence events
