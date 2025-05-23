#!/bin/bash

# === CONFIG ===
N=25000                 # Matrix size
T=16                  # Total number of threads/slaves
LABEL="PCWSL1"                  # Core-affined? 0 = no, 1 = yes
BASE_PORT=28030      # Starting port
EXEC=./a.out         # Compiled binary
SESSION_NAME="lab_slaves"
CONFIG="droneconfig/config_${T}.cfg"
IP_FILE="util/ip_list.txt"

# === Read IPs from file ===
mapfile -t IPS < "$IP_FILE"
PC_IP="${IPS[0]}"
LAPTOP_IP="${IPS[1]}"

# === Get the correct local machine IP ===
ALL_LOCAL_IPS=($(hostname -I))
LOCAL_IP=""

for ip in "${ALL_LOCAL_IPS[@]}"; do
  if [[ "$ip" == "$PC_IP" || "$ip" == "$LAPTOP_IP" ]]; then
    LOCAL_IP="$ip"
    break
  fi
done

if [[ -z "$LOCAL_IP" ]]; then
  echo "❌ Could not match any local IP to entries in $IP_FILE"
  exit 1
fi

echo "📍 Local IP: $LOCAL_IP"
echo "🔍 PC IP: $PC_IP"
echo "🔍 Laptop IP: $LAPTOP_IP"

# === Determine thread range ===
if [[ "$LOCAL_IP" == "$PC_IP" ]]; then
  START=1
  END=$((T / 2))
  echo "🖥️ Running PC-assigned slave threads: $START to $END"
elif [[ "$LOCAL_IP" == "$LAPTOP_IP" ]]; then
  START=$((T / 2 + 1))
  END=$T
  echo "💻 Running Laptop-assigned slave threads: $START to $END"
fi

# === Launch slave processes in tmux split panes ===
SESSION_NAME="lab_slaves"

# === Kill old tmux session if it exists ===
tmux kill-session -t $SESSION_NAME 2>/dev/null
# tmux kill-session -t lab_slaves

# === Start a new tmux session ===
tmux new-session -d -s $SESSION_NAME

first_pane=true

for ((i=START; i<=END; i++)); do
  PORT=$((BASE_PORT + i))
  CMD="$EXEC $N $PORT 1 $T $LABEL"

  if $first_pane; then
    tmux send-keys -t "$SESSION_NAME" "$CMD" C-m
    first_pane=false
  else
    tmux split-window -t "$SESSION_NAME" -v
    tmux select-layout -t "$SESSION_NAME" tiled
    tmux send-keys -t "$SESSION_NAME" "$CMD" C-m
  fi
done

# === Attach to tmux session ===
tmux attach -t $SESSION_NAME