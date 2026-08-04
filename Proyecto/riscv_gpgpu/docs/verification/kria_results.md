# Kria Deployment Results (T055)

**Status**: Pending hardware run — this file is overwritten by
`scripts/deploy_kria.sh` after each deployment.

## How to reproduce

```bash
scripts/deploy_kria.sh \
    --bitstream <gpgpu_kv260.bit.bin> \
    --kernel build-all/kernels/vector_add.elf \
    --test test_host_api \
    --host ubuntu@<kria-ip>
```

Expected outcome: `vector_add` (N=1024) executes on the FPGA GPGPU and all
result elements verify correct; the script records the pass/fail report here.
