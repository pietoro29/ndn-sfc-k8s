from pathlib import Path
import yaml
import subprocess
import shutil
import hashlib

WORK_DIR = Path("/work")
TOPOLOGY_FILE = WORK_DIR / "topology.txt"
POLICY_FILE = WORK_DIR / "nac-setup" / "nac_policy.yaml"
OUT_DIR = WORK_DIR / "nac-setup" / "out"
P12_PASS = "password"

def run_cmd(cmd):
    try:
        subprocess.check_call(cmd, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running cmd: {cmd}")
        raise

def clean_dir(path):
    if path.exists():
        shutil.rmtree(path)
    path.mkdir()

def get_all_nodes():
    topo_path = Path(TOPOLOGY_FILE)

    if not topo_path.exists():
        raise FileNotFoundError(f"Topology file not found: {topo_path}")

    nodes = set()
    with topo_path.open('r') as f:
        for line in f:
            parts = line.strip().split(':')
            if len(parts) >= 1:
                node_id = parts[0].strip()
                nodes.add(f"ndn-node{node_id}")
    return list(nodes)

def load_config(path):
    if not path.exists():
        raise FileNotFoundError(f"Policy not found: {path}")
    with path.open('r') as f:
        return yaml.safe_load(f)

def generate_base_identities(nodes, org_prefix):
    print(f"--- Generating Base Identities for: {nodes} ---")
    node_cert_map = {}
    for node in nodes:
        node_dir = OUT_DIR / node
        node_dir.mkdir(exist_ok=True)

        identity = f"{org_prefix}/{node}"
        p12_path = node_dir / "identity.p12"
        cert_dump_path = node_dir / "self.cert"

        run_cmd(f"ndnsec key-gen -t r {identity} | ndnsec cert-install -")
        run_cmd(f"ndnsec export -P {P12_PASS} -o {p12_path} -i {identity}")
        run_cmd(f"ndnsec cert-dump -i {identity} > {cert_dump_path}")

        node_cert_map[node] = cert_dump_path
    return node_cert_map

def setup_am_identity(am_node, am_base):
    am_dir = OUT_DIR / am_node
    am_dir.mkdir(exist_ok = True)
    am_identity = f"{am_base}/{am_node}"
    am_p12_path = am_dir / "am-identity.p12"

    print(f"[{am_node}] Setting up AM Identity: {am_identity}")
    run_cmd(f"ndnsec key-gen -t r {am_identity} | ndnsec cert-install -")
    run_cmd(f"ndnsec export -P {P12_PASS} -o {am_p12_path} -i {am_identity}")

    return am_identity, am_dir

def generate_kek_and_kdk(am_identity, am_dir, content_config, node_cert_map):
    data_prefix = content_config['prefix']
    consumers = content_config.get('allowed_consumers', [])
    #プレフィックスのハッシュでファイル名を作成
    prefix_hash = hashlib.md5(data_prefix.encode()).hexdigest()[:8]

    kek_filename = f"kek_{prefix_hash}.data"
    kek_path = am_dir / kek_filename

    print(f"  > Generating KEK for {data_prefix} -> {kek_filename}")
    run_cmd(f"ndn-nac dump-kek -i {am_identity} -d {data_prefix} > {kek_path}")

    for consumer in consumers:
        if consumer not in node_cert_map:
            print(f"    ! Warning: Consumer {consumer} not found. Skipping.")
            continue

        consumer_cert = Path(node_cert_map[consumer])

        kdk_filename = f"kdk_{prefix_hash}_{consumer}.data"
        kdk_path = am_dir / kdk_filename

        print(f"    > Generating KDK for {consumer} (Saved in AM dir)")
        run_cmd(f"ndn-nac add-member -i {am_identity} -d {data_prefix} -m {consumer_cert} -o {kdk_path}")

def process_nac_policies(config, node_cert_map):
    print("\n--- Processing NAC Policies ---")
    am_base = config.get('am_prefix_base', '/ndn/AM')
    policies = config.get('policies', [])

    for policy in policies:
        am_node = policy['am_node']
        am_identity, am_dir = setup_am_identity(am_node, am_base)
        for content in policy.get('data_contents', []):
            generate_kek_and_kdk(am_identity, am_dir, content, node_cert_map)

def main():
    print("=== Starting NAC Key Generation ===")

    # 1. 初期化
    clean_dir(OUT_DIR)
    config = load_config(POLICY_FILE)
    all_nodes = get_all_nodes()

    # 2. 基本IDの生成 (全ノード)
    org_prefix = config.get('org_prefix', '')
    node_cert_map = generate_base_identities(all_nodes, org_prefix)

    # 3. NACポリシーの適用 (AM, KEK, KDK)
    process_nac_policies(config, node_cert_map)

    print(f"\n=== Generation Complete. Files are in '{OUT_DIR}' ===")

if __name__ == "__main__":
    main()
