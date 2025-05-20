# Research Activity 5: Distributed Column-wise MSE Computation via Sockets

**Author:** Jakie Ashley C. Brabante  
**Section:** CD-1L  
**Lab Problem:** 5  

## Description  
This program performs **distributed computation of the Mean Square Error (MSE)** by sending **columns of an `n × n` matrix** to slave nodes over **TCP sockets**, along with a reference vector `y`. Each slave computes the **RMSE of assigned columns** in parallel and sends the result vector `e` back to the master. This architecture simulates collaborative data processing across networked nodes and enables performance evaluation under different levels of parallelism.

This activity incorporates:
- **POSIX threads** for parallel execution  
- **Socket programming** for distributed communication  
- **Core affinity** for optimized CPU usage and minimized thread migration  

## Programming Language  
- C

## Dependencies  
- Standard Libraries: `math.h`, `time.h`, `stdlib.h`, `stdio.h`, `stdbool.h`, `errno.h`  
- Networking: `sys/socket.h`, `netinet/in.h`, `arpa/inet.h`, `unistd.h`  
- Threading & Core Affinity: `pthread.h`, `sched.h`  
- Compiler must support GNU extensions (`#define _GNU_SOURCE`)

## Source Code  
- Filename: `BRABANTE_JA_code.c`

## How to Run the Program  

### **Using the Makefile**  
Type one of the following commands in the terminal:

- **`make`**  
  Compiles the program into an executable named `a.out`.

- **`make execute`**  
  Runs the script `execute_runs.sh`, which automatically:
  - Launches `T` slave terminals followed by the master
  - Assigns unique ports for communication
  - Reads from the config file `config_T.cfg`

- **`make clean`**  
  Deletes the compiled output file `a.out`.

### **Using the Bash Script (`execute_runs.sh`)**  
This script automates launching both slave and master processes using `gnome-terminal`.  
You can adjust the following parameters:
- `N`: matrix size (e.g., 25000)
- `T`: number of slave threads (e.g., 2, 4, 8)
- `C`: enable core affinity (`0` = no, `1` = yes)
- `BASE_PORT`: starting port number (e.g., 5000)

## Output  
- **Terminal**: Displays socket creation, connection logs, vector transmission, MSE computation time, and thread-core assignments.  
- **Files**:
  - `Exer5Results.tsv`: machine-readable runtime log with parameters  
  - `Exer5Results_pretty.txt`: table-formatted log for quick viewing

> **Note:** `gnome-terminal` must be installed for this automation to work.

If `n == 15`, the program loads predefined dummy data to validate correctness.  
It prints the full matrix, reference vector `y`, and the resulting error vector `e` for inspection.