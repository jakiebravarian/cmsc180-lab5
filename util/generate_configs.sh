#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IP_FILE="${SCRIPT_DIR}/ip_list.txt"
CONFIG_DIR="${SCRIPT_DIR}/../droneconfig"
PORT_START=28030
THREADS_LIST=(2 4 8 16)

# Read IPs
mapfile -t IPS < "$IP_FILE"
MASTER_IP="${IPS[0]}"

# Create config directory if it doesn't exist
mkdir -p "$CONFIG_DIR"

for t in "${THREADS_LIST[@]}"; do
    CONFIG_FILE="${CONFIG_DIR}/config_${t}.cfg"
    echo "Generating $CONFIG_FILE..."

    {
        echo "${MASTER_IP}:${PORT_START}"
        echo "$t"
        for ((i=1; i<=t; i++)); do
            SLAVE_IP="${IPS[i]}"
            PORT=$((PORT_START + i))
            echo "${SLAVE_IP}:${PORT}"
        done
    } > "$CONFIG_FILE"
done

echo "✅ Configs saved to: $CONFIG_DIR"