import os
import yaml
import subprocess
import shutil
import hashlib

# ディレクトリ設定 (Docker内でのパスを想定)
WORK_DIR = "/work"
TOPOLOGY_FILE = os.path.join(WORK_DIR, "topology.txt")
POLICY_FILE = os.path.join(WORK_DIR, "nac-setup", "nac_policy.yaml")
OUT_DIR = os.path.join(WORK_DIR, "nac-setup", "out")
P12_PASS = "password"

def run_cmd(cmd):
    """コマンド実行ヘルパー"""
    try:
        subprocess.check_call(cmd, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running cmd: {cmd}")
        raise e

def clean_dir(path):
    if os.path.exists(path):
        shutil.rmtree(path)
    os.makedirs(path)

def get_all_nodes():
    """topology.txtから全ノード名を取得"""
    nodes = set()
    if not os.path.exists(TOPOLOGY_FILE):
        print("Topology file not found, assuming default nodes for testing.")
        return ["ndn-node1", "ndn-node2", "ndn-node3"]

    with open(TOPOLOGY_FILE, 'r') as f:
        for line in f:
            parts = line.strip().split(':')
            if len(parts) >= 1:
                node_id = parts[0].strip()
                nodes.add(f"ndn-node{node_id}")
    return list(nodes)

def main():
    print("=== Starting NAC Key Generation ===")
    clean_dir(OUT_DIR)

    # 1. 設定読み込み
    with open(POLICY_FILE, 'r') as f:
        config = yaml.safe_load(f)

    org_prefix = config.get('org_prefix', '/ndn/test')
    am_base = config.get('am_prefix_base', '/ndn/AM')

    # 2. 全ノードの基本Identity作成
    all_nodes = get_all_nodes()
    node_cert_map = {} # Consumerの証明書パスを保持するマップ

    print(f"--- Generating Base Identities for: {all_nodes} ---")
    for node in all_nodes:
        node_dir = os.path.join(OUT_DIR, node)
        os.makedirs(node_dir, exist_ok=True)

        # Identity: /ndn/waseda/labA/<node_name>
        identity = f"{org_prefix}/{node}"
        p12_path = os.path.join(node_dir, "identity.p12")
        cert_dump_path = os.path.join(node_dir, "self.cert")

        print(f"[{node}] Creating identity: {identity}")
        # キー生成 & インストール
        run_cmd(f"ndnsec key-gen -t r {identity} | ndnsec cert-install -")
        # .p12 エクスポート (Pod配布用)
        run_cmd(f"ndnsec export -P {P12_PASS} -o {p12_path} -i {identity}")
        # 証明書ダンプ (AMへ渡す用)
        run_cmd(f"ndnsec cert-dump -i {identity} > {cert_dump_path}")

        node_cert_map[node] = cert_dump_path

    # 3. ポリシーに基づくAM/KEK/KDK生成
    print("\n--- Processing NAC Policies ---")
    for policy in config.get('policies', []):
        am_node = policy['am_node']
        am_dir = os.path.join(OUT_DIR, am_node)

        # AM Identity: /ndn/AM/<node_name>
        am_identity = f"{am_base}/{am_node}" # 例: /ndn/AM/ndn-node1
        am_p12_path = os.path.join(am_dir, "am-identity.p12")

        print(f"[{am_node}] Setting up AM Identity: {am_identity}")
        run_cmd(f"ndnsec key-gen -t r {am_identity} | ndnsec cert-install -")
        run_cmd(f"ndnsec export -P {P12_PASS} -o {am_p12_path} -i {am_identity}")

        # 各データプレフィックス処理
        for content in policy.get('data_contents', []):
            data_prefix = content['prefix']
            consumers = content.get('allowed_consumers', [])

            # KEK生成 (AMノード用)
            # ファイル名はPrefixのハッシュ値を使って一意にする
            prefix_hash = hashlib.md5(data_prefix.encode()).hexdigest()[:8]
            kek_filename = f"kek_{prefix_hash}.data"
            kek_path = os.path.join(am_dir, kek_filename)

            print(f"  > Generating KEK for {data_prefix} -> {kek_filename}")
            run_cmd(f"ndn-nac dump-kek -i {am_identity} -d {data_prefix} > {kek_path}")

            # ConsumerごとのKDK生成
            for consumer in consumers:
                if consumer not in node_cert_map:
                    print(f"    ! Warning: Consumer {consumer} not found in topology. Skipping.")
                    continue

                consumer_cert = node_cert_map[consumer]
                consumer_dir = os.path.join(OUT_DIR, consumer)
                kdk_filename = f"kdk_{prefix_hash}.data" # Consumer側も同じハッシュ名で保存
                kdk_path = os.path.join(consumer_dir, kdk_filename)

                print(f"    > Generating KDK for {consumer}")
                run_cmd(f"ndn-nac add-member -i {am_identity} -d {data_prefix} -m {consumer_cert} -o {kdk_path}")

    print("\n=== Generation Complete. Files are in 'out/' ===")

if __name__ == "__main__":
    main()
