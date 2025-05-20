#!/bin/bash

# === CONFIG ===
N=15           # Matrix size
T=8              # Number of threads/slaves
BASE_PORT=28030   # Starting port
EXEC=./a.out      # Compiled binary
CONFIG="config_${T}.cfg"

# === Start Slaves First ===
for ((i=1; i<=T; i++)); do
  PORT=$((BASE_PORT + i))
  gnome-terminal -- bash -c "$EXEC $N $PORT 1 $T; exec bash"
  sleep 0.5  # small delay to ensure slave is listening
done

# === Start Master ===
gnome-terminal -- bash -c "$EXEC $N $BASE_PORT 0 $T; exec bash"