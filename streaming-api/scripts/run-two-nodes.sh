#!/usr/bin/env bash
# Levanta DOS nodos de streaming en esta misma máquina para probar la replicación (V2.0).
# En la entrega real, cada nodo corre en una máquina distinta: cambie 127.0.0.1 por la IP del otro.
set -euo pipefail
BIN="${BIN:-./build/tecflix_streaming}"
TOKEN="${TECFLIX_INTERNAL_TOKEN:-cambiar-este-secreto}"

TECFLIX_PORT=8083 TECFLIX_NODE_ID=nodeA TECFLIX_STORAGE_DIR=./data/A \
  TECFLIX_PEERS=http://127.0.0.1:8084 TECFLIX_INTERNAL_TOKEN="$TOKEN" "$BIN" &
TECFLIX_PORT=8084 TECFLIX_NODE_ID=nodeB TECFLIX_STORAGE_DIR=./data/B \
  TECFLIX_PEERS=http://127.0.0.1:8083 TECFLIX_INTERNAL_TOKEN="$TOKEN" "$BIN" &
trap 'kill 0' INT TERM
wait
