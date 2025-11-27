FROM icekarinn/ndn-all:v1.0

# Python環境の整備 (ndn-allのベースOSがUbuntu/Debianと仮定)
RUN apt-get update && apt-get install -y python3-pip && rm -rf /var/lib/apt/lists/*

# 必要なPythonライブラリをインストール
RUN pip3 install --no-cache-dir pyyaml

WORKDIR /app

# スクリプトをコピー
COPY generate_nac_keys.py .

# デフォルトコマンド
CMD ["python3", "generate_nac_keys.py"]
