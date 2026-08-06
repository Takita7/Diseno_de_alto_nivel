#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [[ -f "$ROOT_DIR/scripts/setup-env.sh" ]]; then
  # shellcheck disable=SC1091
  source "$ROOT_DIR/scripts/setup-env.sh" >/dev/null 2>&1
fi

if ! command -v vivado >/dev/null 2>&1; then
  echo "ERROR: vivado not found in PATH."
  echo "Run: source scripts/setup-env.sh"
  exit 1
fi

exec vivado -mode gui -source "$ROOT_DIR/fpga/scripts/open_impl_gui.tcl"
