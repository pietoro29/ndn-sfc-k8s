from pathlib import Path
import yaml
import subprocess
import shutil
import hashlib

WORK_DIR = Path("/work")
BASE_OUT_DIR = WORK_DIR / "setup" / "out"
POLICY_FILE = WORK_DIR / "nac-setup" / "nac_policy.yaml"
P12_PASS = "password"

def run_cmd(cmd):
    try:
        subprocess.check_call(cmd, shell=True)
    except subprocess.CalledProcessError as e:
        print(f"Error running cmd: {cmd}")
        raise

def load_config(path):
    if not path.exists():
        raise FileNotFoundError(f"Policy not found: {path}")
    with path.open('r') as f:
        return yaml.safe_load(f) #yamlをpythonのdictやlistに変換する。{"policies":[{"am_node": "ndn-node1"}]}的な

def map_existing_nodes():
    node_cert_map = {}
    if not BASE_OUT_DIR.exists():
         raise FileNotFoundError(f"Base output directory not found at {BASE_OUT_DIR}. Run setup/setup.sh first.")

    for node_dir in BASE_OUT_DIR.iterdir():
        if node_dir.is_dir():
            node_name = node_dir.name
            cert_path = node_dir / "self.cert"
            if cert_path.exists():
                node_cert_map[node_name] = cert_path
    return node_cert_map

def setup_am_identity(am_node, am_base):
    am_dir = BASE_OUT_DIR / am_node
    if not am_dir.exists():
        am_dir.mkdir(parents=True, exist_ok=True)
    am_identity = f"{am_base}/{am_node}"
    am_p12_path = am_dir / "am-identity.p12"

    print(f"[{am_node}] Setting up AM Identity: {am_identity}")
    run_cmd(f"ndnsec key-gen -t r {am_identity} | ndnsec cert-install -")
    run_cmd(f"ndnsec export -P {P12_PASS} -o {am_p12_path} -i {am_identity}")

    return am_identity, am_dir

def generate_kek_and_kdk(am_identity, am_dir, content_config, node_cert_map):
    data_prefix = content_config['prefix']
    consumers = content_config.get('allowed_consumers', [])
    prefix_hash = hashlib.md5(data_prefix.encode()).hexdigest()[:8]

    kek_filename = f"kek_{prefix_hash}.data"
    kek_path = am_dir / kek_filename

    print(f"  > Generating KEK for {data_prefix} -> {kek_filename}")
    run_cmd(f"ndn-nac dump-kek -i {am_identity} -d {data_prefix} > {kek_path}")

    for consumer in consumers:
        if consumer not in node_cert_map:
            print(f"    ! Warning: Consumer {consumer} not found. Skipping.")
            continue

        consumer_cert = node_cert_map[consumer]
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

    config = load_config(POLICY_FILE)
    node_cert_map = map_existing_nodes()
    print(f"Loaded {len(node_cert_map)} nodes from base setup.")

    process_nac_policies(config, node_cert_map)

    print(f"\n=== NAC Generation Complete. Files added to '{BASE_OUT_DIR}' ===")

if __name__ == "__main__":
    main()
