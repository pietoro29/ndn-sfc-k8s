#!/bin/bash
set -e
cd "$(dirname "$0")"
WORK_DIR=$(pwd)
MESH_ROOT=$(dirname "$WORK_DIR")
OUT_DIR="$WORK_DIR/out"
IMAGE="icekarinn/nac-generator:v1.2"

rm -rf "$OUT_DIR"

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

echo "=== 2. Generating Keys ==="
run_generator generate_nac_keys.py

echo "=== 2.5. Generating NLSR Advertising Config ==="
run_generator generate_nlsr_advertise_config.py

echo "=== 3. Creating Kubernetes Secrets ==="
kubectl delete secret -l type=ndn-node-secret || true

for NODE_PATH in "$OUT_DIR"/*; do
    [ -d "$NODE_PATH" ] || continue
    NODE_NAME=$(basename "$NODE_PATH")
    SECRET_NAME="ndn-secret-${NODE_NAME}"

    echo "Creating secret $SECRET_NAME ..."
    kubectl create secret generic "$SECRET_NAME" \
        --from-file="$NODE_PATH" \
        --dry-run=client -o yaml | kubectl apply -f -

    kubectl label secret "$SECRET_NAME" type=ndn-node-secret --overwrite
done

echo "=== NAC Setup Finished ==="
