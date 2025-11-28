#!/bin/bash

set -e
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

GENERATOR_IMAGE="icekarinn/ndn-valuesyaml-generator:1.1"

echo "=== Generating values.yaml from topology.txt ==="
docker run --rm \
  -u "$(id -u):$(id -g)" \
  -v "${SCRIPT_DIR}:/mesh" \
  -w /mesh/generator \
  $GENERATOR_IMAGE \
  python generate_values.py
echo "values.yaml generated."

echo "=== Syncing App Source Code ==="
# ソースコードをHelmチャート内にコピーする
# これにより、開発は /mesh/apps で行い、デプロイ時に manifest に反映される
mkdir -p "${SCRIPT_DIR}/manifest/apps"
cp "${SCRIPT_DIR}/apps/"* "${SCRIPT_DIR}/manifest/apps/"

echo "=== Installing Helm Chart ==="
# 既存のものを削除
if helm status ndn-cluster > /dev/null 2>&1; then
    echo "Uninstalling existing release..."
    helm uninstall ndn-cluster
    sleep 2
fi

helm install ndn-cluster "${SCRIPT_DIR}/manifest"
echo "Helm install completed."

echo "=== Waiting for all nodes to be ready ==="
kubectl wait --for=condition=ready pod -l "app" --timeout=300s
echo "All nodes are ready."

echo "=== Starting NDN Services (NFD/NLSR) inside Pods ==="
PODS=$(kubectl get pods --field-selector=status.phase=Running -o name | grep ndn-node)

for pod in $PODS; do
    # Pod名からノードIDなどを抽出 (表示用)
    NODE_NAME=$(echo $pod | sed 's|pod/||' | cut -d'-' -f1-2)

    echo "Starting services on $NODE_NAME..."

    # kubectl exec で内部コマンドを一気に流し込む
    # nohup & を使うことで、スクリプト終了後もプロセスを維持
    kubectl exec $pod -- /bin/bash -c "
        # 証明書セットアップ
        ndnsec key-gen /$NODE_NAME | ndnsec cert-install - || true

        # NFD起動 (二重起動防止)
        if ! pgrep nfd > /dev/null; then
            mkdir -p /run/nfd
            nohup nfd-start > /nfd.log 2>&1 &
            sleep 2
        fi

        # NLSR起動
        if ! pgrep nlsr > /dev/null; then
            mkdir -p /tmp/nlsr
            #Helm由来のベース設定をコピー
            cp /etc/nlsr/nlsr.conf /tmp/nlsr/nlsr.conf
            #NAC由来のadvertising設定があれば追記結合
            if [ -f /data/nac-data/nlsr-advertising.conf ]; then
                echo \"Merging NAC advertising config...\"
                echo \"\" >> /tmp/nlsr/nlsr.conf
                cat /data/nac-data/nlsr-advertising.conf >> /tmp/nlsr/nlsr.conf
            else
                echo \"No NAC advertising config found. Using base config only.\"
            fi

            nohup nlsr -f /tmp/nlsr/nlsr.conf > /nlsr.log 2>&1 &
        fi

        # Face作成 (環境変数 NEIGHBORS はHelmで注入済み)
        (
            for neighbor in \$NEIGHBORS; do
                until nfdc face create tcp4://\$neighbor; do
                    sleep 3
                done
            done
        ) > /face-create.log 2>&1 &
    "
done

echo "=== All Done! NDN Mesh is up and running. ==="
