#!/bin/bash
set -e
cd "$(dirname "$0")"
WORK_DIR=$(pwd)
MESH_ROOT=$(dirname "$WORK_DIR")
OUT_DIR="${MESH_ROOT}/setup/out"
IMAGE="icekarinn/nac-generator:v1.4"

run_generator() {
  local script="$1"
  echo ">>> Running: $script"

  docker run --rm \
    -v "${MESH_ROOT}:/work" \
    --user "$(id -u):$(id -g)" \
    --env HOME=/tmp \
    "$IMAGE" \
    python3 "$script"
}

echo "=== Running NAC Key Add-on ==="
run_generator /work/nac-setup/generate_nac_keys.py

echo "=== Generating NLSR Advertising Config (NAC Part) ==="
run_generator /work/nac-setup/generate_nlsr_advertise_config.py
