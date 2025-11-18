import yaml
import os

THIS_DIR = os.path.dirname(os.path.abspath(__file__)) #/mesh/generator
MESH_DIR = os.path.dirname(THIS_DIR) #/mesh
TOPOLOGY_FILE = os.path.join(MESH_DIR, 'topology.txt') #/mesh/topology.txt
MANIFEST_DIR = os.path.join(MESH_DIR, 'manifest')
VALUEYAML_FILE = os.path.join(MANIFEST_DIR, 'values.yaml') #/mesh/manifest/values.yaml

nodes = []
try:
    with open(TOPOLOGY_FILE, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            parts = line.split(':')
            if len(parts) != 2:
                continue

            #1: 2 4ならば"1"と["2", "4"]になる
            node_id = parts[0].strip()
            neighbor_ids = parts[1].strip().split()

            #Helmテンプレート用に整形
            node_name = f"ndn-node{node_id}"
            neighbors_str = " ".join([f"ndn-node{nid}" for nid in neighbor_ids])

            nodes.append({
                "node_name": node_name,
                "neighbors": neighbors_str
            })

    #values.yamlに書き込む部分
    values_data = {
        "topology" : nodes
    }

    #values.yamlを作って実際に書き込み
    with open(VALUEYAML_FILE, 'w') as f:
        yaml.dump(values_data, f, default_flow_style=False)

    #成功したら表示
    print(f"Successfully generated '{VALUEYAML_FILE}' from '{TOPOLOGY_FILE}'.")
    print("\nGenerated content:")
    print(yaml.dump(values_data, default_flow_style=False))

except FileNotFoundError:
    print(f"Error: '{TOPOLOGY_FILE}' not found. Please create it at that location.")
except Exception as e:
    print(f"An error occurred: {e}")

