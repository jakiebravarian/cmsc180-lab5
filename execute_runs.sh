#!/bin/bash

# === CONFIG ===
N=20000           # Matrix size
T=2               # Number of threads/slaves
C=0               # Core-affined? 0 = no, 1 = yes
BASE_PORT=28030   # Starting port
EXEC=./a.out      # Compiled binary
CONFIG="config_${T}.cfg"

# === Start Slaves First ===
for ((i=1; i<=T; i++)); do
  PORT=$((BASE_PORT + i))
  gnome-terminal -- bash -c "$EXEC $N $PORT 1 $T $C; exec bash"
  sleep 0.5  # small delay to ensure slave is listening
done

# === Start Master ===
gnome-terminal -- bash -c "$EXEC $N $BASE_PORT 0 $T $C; exec bash"