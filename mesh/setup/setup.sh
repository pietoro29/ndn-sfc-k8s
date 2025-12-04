#!/bin/bash
set -e
cd "$(dirname "$0")"
WORK_DIR=$(pwd)
MESH_ROOT=$(dirname "$WORK_DIR")
OUT_DIR="$WORK_DIR/out"

IMAGE="icekarinn/identity-generator:v1.4"

rm -rf "$OUT_DIR"

echo "=== Generating Base NDN Identities ==="
docker run --rm \
  -v "${MESH_ROOT}:/work" \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp \
  "$IMAGE" \
  python3 /work/setup/generate_identities.py
