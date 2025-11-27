#!/bin/bash
set -e

# 引数や環境変数で受け取る
# create.sh から docker run -e ROOT_NODE=... で渡される想定
ROOT_NODE=${ROOT_NODE}
SITE_NODE=${SITE_NODE}
OP_NODE=${OP_NODE}
NODES="$@" # 引数の残りはノードリスト

OUT_DIR="/out"
mkdir -p $OUT_DIR/common

#Generatorコンテナ上で全ての鍵、証明書をOUT_DIR/に事前に作る。
echo "--- 1. CA Keys Generation (Root/Site/Op) ---"
# Root
ndnsec key-gen /ndn > $OUT_DIR/common/root.key
ndnsec cert-dump -i /ndn > $OUT_DIR/common/root.cert

# Site
ndnsec key-gen /ndn/jp > $OUT_DIR/common/site.key
ndnsec cert-gen -s /ndn -i $OUT_DIR/common/site.key > $OUT_DIR/common/site.cert
ndnsec cert-install -f $OUT_DIR/common/site.cert

# Operator
ndnsec key-gen /ndn/jp/%C1.Operator/op > $OUT_DIR/common/op.key
ndnsec cert-gen -s /ndn/jp -i $OUT_DIR/common/op.key > $OUT_DIR/common/op.cert
ndnsec cert-install -f $OUT_DIR/common/op.cert

#それぞれのrouterにおけるkeyを作る
echo "--- 2. Router Keys & Distribution ---"
for NODE in $NODES; do
    NODE_DIR="$OUT_DIR/$NODE"
    mkdir -p $NODE_DIR

    SHORT_NAME=${NODE#ndn-}
    ROUTER_ID="/ndn/jp/%C1.Router/$SHORT_NAME"

    # Router Key作成
    ndnsec key-gen $ROUTER_ID > $NODE_DIR/temp.key
    ndnsec cert-gen -s /ndn/jp/%C1.Operator/op -i $NODE_DIR/temp.key > $NODE_DIR/router.cert
    ndnsec export -P "" -o $NODE_DIR/identity.ndnkey -i $ROUTER_ID

    # 【全員共通】信頼アンカーとして Root Cert は持っておく
    cp $OUT_DIR/common/root.cert $NODE_DIR/root.cert

    # 【役割に応じた配布】
    # Root担当ノードなら、公開用に root.cert を配置
    if [ "$NODE" == "$ROOT_NODE" ]; then
        cp $OUT_DIR/common/root.cert $NODE_DIR/root-publish.cert
    fi

    # Site担当ノードなら
    if [ "$NODE" == "$SITE_NODE" ]; then
        cp $OUT_DIR/common/site.cert $NODE_DIR/site-publish.cert
    fi

    # Operator担当ノードなら
    if [ "$NODE" == "$OP_NODE" ]; then
        cp $OUT_DIR/common/op.cert $NODE_DIR/op-publish.cert
    fi
done
