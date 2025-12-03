#!/bin/bash
set -e

PREFIX="/ndn/jp/waseda/labA/homepage/index.html"
PRODUCER_LABEL="app=ndn-node1"
CONSUMER_LABEL="app=ndn-node3"

COMPILE_CMD='g++ -o /tmp/bin /usr/src/app/FILE.cpp -std=c++17 $(pkg-config --cflags --libs libndn-cxx) -lndn-nac'

echo "=== 1. Finding Pods ==="
POD1=$(kubectl get pod -l $PRODUCER_LABEL -o jsonpath="{.items[0].metadata.name}")
POD3=$(kubectl get pod -l $CONSUMER_LABEL -o jsonpath="{.items[0].metadata.name}")

if [ -z "$POD1" ] || [ -z "$POD3" ]; then
    echo "Error: Could not find producer or consumer pod."
    exit 1
fi

echo "Producer Pod: $POD1"
echo "Consumer Pod: $POD3"

echo "=== 2. Setting up Producer (Node 1) ==="

echo "Compiling producer.cpp on $POD1..."
kubectl exec "$POD1" -- sh -c "$(echo $COMPILE_CMD | sed 's/FILE/producer/')"
kubectl exec "$POD1" -- sh -c "mv /tmp/bin /tmp/producer"
echo "Starting producer in background..."
kubectl exec "$POD1" -- sh -c "export NDN_DATA_PREFIX='$PREFIX' && nohup /tmp/producer > /tmp/producer.log 2>&1 &"
echo "Producer started. Waiting 5 seconds for initialization..."
sleep 5

echo "=== 3. Setting up Consumer (Node 3) ==="
echo "Compiling consumer.cpp on $POD3..."
kubectl exec "$POD3" -- sh -c "$(echo $COMPILE_CMD | sed 's/FILE/consumer/')"
kubectl exec "$POD3" -- sh -c "mv /tmp/bin /tmp/consumer"

echo "Running consumer..."
echo "----------------------------------------"
kubectl exec "$POD3" -- sh -c "export NDN_DATA_PREFIX='$PREFIX' && /tmp/consumer"
echo "----------------------------------------"

echo "=== Experiment Finished ==="
echo "producer logs:"
echo "kubectl exec $POD1 -- cat /tmp/producer.log"
