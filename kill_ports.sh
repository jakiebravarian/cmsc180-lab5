#!/bin/bash

for port in $(seq 28030 28046); do
  pid=$(lsof -ti :$port)
  if [ -n "$pid" ]; then
    echo "Killing PID $pid on port $port"
    kill -9 $pid
  fi
done