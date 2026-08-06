#!/usr/bin/env bash
# setup_kria_fpga_mem.sh - ONE-TIME board setup: reserve 64 MiB at 0x60000000
# for the RISC-V GPGPU FPGA demo (makes the region accessible via /dev/mem).
#
# Run this ONCE on the Kria board then reboot:
#   scp scripts/setup_kria_fpga_mem.sh ubuntu@kria:~
#   ssh ubuntu@kria 'SUDO_PASS=petalinux bash ~/setup_kria_fpga_mem.sh'
#   ssh ubuntu@kria 'echo petalinux | sudo -S reboot'

set -euo pipefail

# Use SUDO_PASS env var when running via SSH (no TTY).
_sudo() { echo "${SUDO_PASS:-}" | sudo -S "$@"; }

MEMMAP_ARG="memmap=64M\\\$0x60000000"

if grep -q "memmap=64M\$0x60000000" /proc/cmdline 2>/dev/null; then
    echo "[OK] DDR region 0x60000000+64MiB is already reserved - no changes needed"
    exit 0
fi

echo "Reserving 64 MiB at 0x60000000 for FPGA RISC-V kernel memory..."

if [ -f /boot/extlinux/extlinux.conf ]; then
    # Kria Ubuntu uses U-Boot extlinux (preferred, most reliable on KV260)
    if grep -q 'memmap=64M' /boot/extlinux/extlinux.conf; then
        echo "[SKIP] memmap already in /boot/extlinux/extlinux.conf"
    else
        # Append to every APPEND line (single quotes avoid bash $ expansion)
        _sudo sed -i '/^[[:space:]]*APPEND/s/$/ memmap=64M$0x60000000/' /boot/extlinux/extlinux.conf
        echo "[DONE] Added memmap to extlinux.conf. Reboot required."
        grep 'APPEND' /boot/extlinux/extlinux.conf
    fi
elif [ -f /etc/default/grub ] && command -v update-grub >/dev/null 2>&1; then
    # Standard Ubuntu with GRUB (fallback)
    if grep -q 'memmap=64M' /etc/default/grub; then
        echo "[SKIP] memmap already in /etc/default/grub"
    else
        _sudo sed -i 's|GRUB_CMDLINE_LINUX_DEFAULT="|GRUB_CMDLINE_LINUX_DEFAULT="memmap=64M$0x60000000 |' /etc/default/grub
        _sudo update-grub
        echo "[DONE] Added memmap to GRUB. Reboot required."
    fi
else
    echo "ERROR: cannot find /boot/extlinux/extlinux.conf or GRUB config" >&2
    echo "  Add manually: memmap=64M\$0x60000000 to kernel boot args" >&2
    exit 1
fi

echo ""
echo "After reboot, verify with:"
echo "  grep memmap /proc/cmdline"
echo "  dd if=/dev/mem bs=4096 skip=\$((0x60000000/4096)) count=1 of=/dev/null 2>&1"
