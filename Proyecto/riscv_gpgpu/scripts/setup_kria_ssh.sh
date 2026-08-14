#!/usr/bin/env bash
set -euo pipefail

# One-time SSH bootstrap for ECASLab bastion + Kria board.
# It will:
# 1) Ensure an SSH key exists
# 2) Install the public key on bastion
# 3) Install the public key on Kria through ProxyJump
# 4) Add Host aliases to ~/.ssh/config (idempotent)

BASTION_HOST="login.cluster.ecaslab.org"
BASTION_ALT_HOST="51.161.8.220"
BASTION_PORT="9222"
BASTION_USER="fpgaitcr"

KRIA_HOST="192.168.1.84"
KRIA_USER="ubuntu"

KEY_PATH="${HOME}/.ssh/id_ed25519"
SSH_CONFIG="${HOME}/.ssh/config"
CONFIG_TAG_BEGIN="# >>> KRIA-ECASLAB >>>"
CONFIG_TAG_END="# <<< KRIA-ECASLAB <<<"

usage() {
  cat <<'EOF'
Usage: setup_kria_ssh.sh [options]

Options:
  --bastion-host HOST   Bastion hostname or IP (default: login.cluster.ecaslab.org)
  --bastion-alt-host H  Alternative bastion host/IP fallback (default: 51.161.8.220)
  --bastion-port PORT   Bastion SSH port (default: 9222)
  --bastion-user USER   Bastion user (default: fpgaitcr)
  --kria-host HOST      Kria private IP (default: 192.168.1.84)
  --kria-user USER      Kria user (default: ubuntu)
  --key-path PATH       Private key path (default: ~/.ssh/id_ed25519)
  -h, --help            Show this help

Notes:
  - The first run can prompt for passwords.
  - Kria default password (if unchanged) is typically: petalinux
  - After setup, use: ssh kria
EOF
}

log() {
  printf '[setup_kria_ssh] %s\n' "$*"
}

check_tcp() {
  local host="$1"
  local port="$2"

  if command -v timeout >/dev/null 2>&1; then
    timeout 5 bash -c "</dev/tcp/${host}/${port}" >/dev/null 2>&1
  else
    bash -c "</dev/tcp/${host}/${port}" >/dev/null 2>&1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --bastion-host)
      BASTION_HOST="$2"; shift 2 ;;
    --bastion-alt-host)
      BASTION_ALT_HOST="$2"; shift 2 ;;
    --bastion-port)
      BASTION_PORT="$2"; shift 2 ;;
    --bastion-user)
      BASTION_USER="$2"; shift 2 ;;
    --kria-host)
      KRIA_HOST="$2"; shift 2 ;;
    --kria-user)
      KRIA_USER="$2"; shift 2 ;;
    --key-path)
      KEY_PATH="$2"; shift 2 ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1 ;;
  esac
done

mkdir -p "${HOME}/.ssh"
chmod 700 "${HOME}/.ssh"

if [[ ! -f "${KEY_PATH}" ]]; then
  log "SSH key not found at ${KEY_PATH}. Generating ed25519 key."
  ssh-keygen -t ed25519 -C "kria-access" -f "${KEY_PATH}"
else
  log "Using existing key: ${KEY_PATH}"
fi

if [[ ! -f "${KEY_PATH}.pub" ]]; then
  echo "Public key not found: ${KEY_PATH}.pub" >&2
  exit 1
fi

BASTION_TARGET="${BASTION_HOST}"
if ! check_tcp "${BASTION_TARGET}" "${BASTION_PORT}"; then
  log "Primary bastion target ${BASTION_TARGET}:${BASTION_PORT} unreachable, trying fallback ${BASTION_ALT_HOST}:${BASTION_PORT}"
  if check_tcp "${BASTION_ALT_HOST}" "${BASTION_PORT}"; then
    BASTION_TARGET="${BASTION_ALT_HOST}"
    log "Using fallback bastion target: ${BASTION_TARGET}:${BASTION_PORT}"
  else
    echo "ERROR: cannot reach bastion on port ${BASTION_PORT} via ${BASTION_HOST} or ${BASTION_ALT_HOST}." >&2
    echo "Check VPN/firewall/routing, then retry." >&2
    exit 2
  fi
fi

log "Installing public key on bastion ${BASTION_USER}@${BASTION_TARGET}:${BASTION_PORT}"
log "If prompted, enter the bastion password for ${BASTION_USER}."
cat "${KEY_PATH}.pub" | ssh \
  -o ConnectTimeout=15 \
  -o ConnectionAttempts=1 \
  -o PreferredAuthentications=publickey,password,keyboard-interactive \
  -o PubkeyAuthentication=yes \
  -o StrictHostKeyChecking=accept-new \
  -p "${BASTION_PORT}" \
  "${BASTION_USER}@${BASTION_TARGET}" \
  'mkdir -p ~/.ssh && chmod 700 ~/.ssh && touch ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys && cat >> ~/.ssh/authorized_keys'

log "Installing public key on Kria ${KRIA_USER}@${KRIA_HOST} via bastion"
log "If prompted, enter the Kria password for ${KRIA_USER} (default often: petalinux)."
cat "${KEY_PATH}.pub" | ssh \
  -o ConnectTimeout=15 \
  -o ConnectionAttempts=1 \
  -o PreferredAuthentications=publickey,password,keyboard-interactive \
  -o PubkeyAuthentication=yes \
  -o StrictHostKeyChecking=accept-new \
  -J "${BASTION_USER}@${BASTION_TARGET}:${BASTION_PORT}" \
  "${KRIA_USER}@${KRIA_HOST}" \
  'mkdir -p ~/.ssh && chmod 700 ~/.ssh && touch ~/.ssh/authorized_keys && chmod 600 ~/.ssh/authorized_keys && cat >> ~/.ssh/authorized_keys'

log "Updating ${SSH_CONFIG} with aliases: ecaslab-bastion, kria"
if [[ -f "${SSH_CONFIG}" ]] && grep -Fq "${CONFIG_TAG_BEGIN}" "${SSH_CONFIG}"; then
  tmp_file="$(mktemp)"
  awk -v b="${CONFIG_TAG_BEGIN}" -v e="${CONFIG_TAG_END}" '
    BEGIN {skip=0}
    index($0,b)==1 {skip=1; next}
    index($0,e)==1 {skip=0; next}
    skip==0 {print}
  ' "${SSH_CONFIG}" > "${tmp_file}"
  mv "${tmp_file}" "${SSH_CONFIG}"
fi

cat >> "${SSH_CONFIG}" <<EOF
${CONFIG_TAG_BEGIN}
Host ecaslab-bastion
  HostName ${BASTION_TARGET}
    User ${BASTION_USER}
    Port ${BASTION_PORT}
    IdentityFile ${KEY_PATH}
    IdentitiesOnly yes
    StrictHostKeyChecking accept-new

Host kria
    HostName ${KRIA_HOST}
    User ${KRIA_USER}
    ProxyJump ecaslab-bastion
    IdentityFile ${KEY_PATH}
    IdentitiesOnly yes
    ServerAliveInterval 30
    ServerAliveCountMax 3
    StrictHostKeyChecking accept-new
${CONFIG_TAG_END}
EOF

chmod 600 "${SSH_CONFIG}"

log "Done. Test with: ssh kria"
log "Copy files with: scp <local_file> kria:/home/${KRIA_USER}/"
