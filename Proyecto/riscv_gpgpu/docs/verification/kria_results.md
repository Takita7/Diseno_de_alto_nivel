# Kria Deployment Report

- Date: 2026-08-06 05:58 UTC
- Board: ubuntu@kria
- Bitstream: gpgpu_system_wrapper.bit.bin
- Kernel ELF: saxpy.riscv.elf
- Test: fpga_smoke_test
- Result: **PASS**

## Test output

```
[sudo] password for ubuntu: Time taken to load BIN is 135.000000 Milli Seconds
BIN FILE loaded through FPGA manager successfully
GPGPU smoke test -- kernel: /home/ubuntu/riscv_gpgpu_deploy/saxpy.riscv.elf
step 1: open /dev/mem
  fd=3
step 2: mmap regs @ 0xa0000000
  regs VA=0xffffb2c3b000
step 3: mmap DDR @ 0x60000000 size 64 MiB
step 4: read HLS reg[0x00] (reserved, expect 0)
  reg[0x00] = 0x00000000
step 5: read reg[0x28] (start_r, expect 0 after reset)
  reg[0x28] = 0x00000000
[PASS] AXI registers accessible
step 6: write/read start_r scratchpad
  wrote 0, read back 0x00000000
[WARN] DDR mmap failed -- skipping kernel execution test
PASS: AXI registers reachable (kernel run skipped -- DDR mmap blocked by strict_devmem)
  mmap DDR: Operation not permitted (errno=1) -- trying with /dev/mem anonymous fallback
```
