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
        #init-containerで証明書は既にimportされていることを確認
        EXISTING_ID=\$(ndnsec list 2>/dev/null | grep -o \"/.*\" | head -n 1)
        if [ -n \"\$EXISTING_ID\" ]; then
             echo \"Found identity prepared by InitContainer: \$EXISTING_ID\"
             ndnsec set-default \$EXISTING_ID
        else
             echo \"Error: No identity found in /data/.ndn (HOME=\$HOME).\"
             echo \"Debug: Listing /data/.ndn content:\"
             ls -R /data/.ndn
             echo \"Debug: Output of ndnsec list:\"
             ndnsec list
             exit 1
        fi

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

        if ls /data/nac-data/kdk_*.data 1> /dev/null 2>&1; then
            echo \"=== AM Node Detected (KDK files found) ===\"

            # すでに起動していない場合のみ実行
            if ! pgrep kdk-server > /dev/null; then
                echo \"Compiling KDK Server...\"
                # pkg-configの実行結果をコンテナ内で展開するために \$() を使用
                g++ -o /usr/local/bin/kdk-server /usr/src/app/kdk-server.cpp \
                    \$(pkg-config --cflags --libs libndn-cxx) -std=c++17

                if [ \$? -eq 0 ]; then
                    echo \"Starting KDK Server in background...\"
                    nohup /usr/local/bin/kdk-server > /data/kdk-server.log 2>&1 &
                else
                    echo \"Error: Failed to compile KDK Server\"
                fi
            else
                 echo \"KDK Server is already running.\"
            fi
        else
            echo \"=== Consumer/Producer Node (No KDK files) ===\"
        fi
    "
done

echo "=== All Done! NDN Mesh is up and running. ==="
