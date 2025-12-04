import yaml
from pathlib import Path
import shutil
import subprocess

WORK_DIR = Path("/work")
TOPOLOGY_FILE = WORK_DIR / "topology.txt"
SETUP_DIR = WORK_DIR / "setup"
CONFIG_FILE = SETUP_DIR / "config.yaml"
OUT_DIR = SETUP_DIR / "out"
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
    path.mkdir(parents=True, exist_ok=True)

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
                if node_id:
                    nodes.add(f"ndn-node{node_id}")
    return list(sorted(nodes))

def load_config(path):
    if not path.exists():
        return {"org_prefix": "/ndn/test"}
    with path.open('r') as f:
        return yaml.safe_load(f)

def generate_identities(nodes, org_prefix):
    print(f"--- Generating Base Identities (Prefix: {org_prefix}) ---")

    for node in nodes:
        node_dir = OUT_DIR / node
        node_dir.mkdir(exist_ok=True)

        identity = f"{org_prefix}/{node}"
        p12_path = node_dir / "identity.p12"
        cert_dump_path = node_dir / "self.cert"

        print(f"[{node}] Generating {identity} ...")
        run_cmd(f"ndnsec key-gen -t r {identity} | ndnsec cert-install -")
        run_cmd(f"ndnsec export -P {P12_PASS} -o {p12_path} -i {identity}")
        run_cmd(f"ndnsec cert-dump -i {identity} > {cert_dump_path}")

def main():
    print("=== Starting Base Identity Generation ===")
    clean_dir(OUT_DIR)

    nodes = get_all_nodes()
    config = load_config(CONFIG_FILE)
    org_prefix = config.get('org_prefix', '/ndn/test')

    generate_identities(nodes, org_prefix)
    print(f"=== Generation Complete. Files are in '{OUT_DIR}' ===")

if __name__ == "__main__":
    main()
