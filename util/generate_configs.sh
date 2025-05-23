#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IP_FILE="${SCRIPT_DIR}/ip_list.txt"
CONFIG_DIR="${SCRIPT_DIR}/../droneconfig"
PORT_START=28030
THREADS_LIST=(1 2 4 8 16)

# Read IPs
mapfile -t IPS < "$IP_FILE"
LAPTOP_IP="${IPS[0]}"
PC_IP="${IPS[1]}"
# MASTER_IP="$PC_IP"
MASTER_IP="$LAPTOP_IP"

# Create config directory if it doesn't exist
mkdir -p "$CONFIG_DIR"

for t in "${THREADS_LIST[@]}"; do
    CONFIG_FILE="${CONFIG_DIR}/config_${t}.cfg"
    echo "Generating $CONFIG_FILE..."

    {
        echo "${MASTER_IP}:${PORT_START}"  # Master port
        echo "$t"                          # Number of threads

        for ((i=1; i<=t; i++)); do
            PORT=$((PORT_START + i))
            if (( i <= t / 2 )); then
                echo "${LAPTOP_IP}:${PORT}"
            else
                echo "${PC_IP}:${PORT}"
            fi
        done
    } > "$CONFIG_FILE"
done

echo "✅ Configs saved to: $CONFIG_DIR"
