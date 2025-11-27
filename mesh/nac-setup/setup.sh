#!/bin/bash
set -e
cd "$(dirname "$0")"
WORK_DIR=$(pwd)
MESH_ROOT=$(dirname "$WORK_DIR")
OUT_DIR="$WORK_DIR/out"

# 生成物をクリア
rm -rf "$OUT_DIR"

echo "=== 1. Building Generator Image ==="
# ここでDockerfileをビルド
docker build -t local/nac-generator:latest .

echo "=== 2. Generating Keys ==="
# ビルドしたイメージを実行
# topology.txtを読むために親ディレクトリ(MESH_ROOT)をマウント
# コンテナ内の /app にスクリプトがあるので、WORKDIRの変更は不要だが
# マウントパスの調整が必要
docker run --rm \
  -v "${MESH_ROOT}:/work" \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp \
  local/nac-generator:latest \
  python3 generate_nac_keys.py

echo "=== 3. Creating Kubernetes Secrets ==="
# 既存のSecretを削除
kubectl delete secret -l type=ndn-node-secret || true

# ディレクトリごとにSecret作成
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
