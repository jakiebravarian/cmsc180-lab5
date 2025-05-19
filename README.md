# Research Activity 4: Socket-based Distribution of Matrix Data

**Author:** Jakie Ashley C. Brabante  
**Section:** CD-1L  
**Lab Problem:** 4  

## Description  
This program implements a **master-slave architecture** using **TCP sockets** to distribute rows of an `n × n` matrix across multiple slave threads. Each slave receives its assigned submatrix from the master, with data transmission handled over the network via socket connections. Threads may optionally be **core-affined** using `pthread_setaffinity_np()` to measure the impact of CPU core binding on runtime performance.

This activity demonstrates the use of **POSIX threads**, **core affinity**, and **network communication** to simulate distributed matrix processing.

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
  - Assigns different ports to each thread
  - Uses the corresponding config file `config_T.cfg`

- **`make clean`**  
  Deletes the compiled output file `a.out`.

### **Using the Bash Script (`execute_runs.sh`)**  
This script automates the launch of slave and master processes using `gnome-terminal`.  
Adjustable parameters in the script:
- `N`: matrix size (e.g., 25000)
- `T`: number of slave threads (e.g., 2, 4, 8)
- `C`: enable core affinity (`0` = no, `1` = yes)
- `BASE_PORT`: port to begin assigning from (e.g., 5000)

## Output  
- **Terminal**: Displays socket setup, thread-core mapping (if enabled), matrix transmission logs, and timing for distribution.
- **Files**:
  - `Exer4Results.tsv`: machine-readable log of runtime and parameters  
  - `Exer4Results_pretty.txt`: human-readable, aligned table of results

> **Note:** `gnome-terminal` must be installed for this to work.

If `n == 15`, the program loads predefined dummy data to allow easy validation of data transmission.  
The matrix and received submatrices are printed on the console for inspection.