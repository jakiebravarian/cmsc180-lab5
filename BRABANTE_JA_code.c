// Author: Jakie Ashley C. Brabante
// Section: CD-1L
// Lab Problem: 5
// Description: Distributed Computation of the Mean Square Error of a Column in a Matrix and a Vector
////////////////////////////////////////

#define DASHES "===================="
#define _GNU_SOURCE 

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <sched.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <errno.h> 

#define IP_LENGTH 16
#define KB 1024

////////////////////////////////////////

typedef struct {
    char *ip;
    int port;
} address;

typedef struct {
    double **matrix;
    double *results;
    int n;
    int start_index; 
    int end_index;
    int t_number;
    address *slave_address;
} master_args_t;

typedef struct {
    int sockfd;  
    int connfd;  
    socklen_t addr_len;      
} SocketConnection;

typedef struct {
    double **X;
    double *y;
    int n;
    int start_index;
    int end_index;
    double *results;
} mse_args_t;

////////////////////////////////////////

// function declarations
void master(int n, int p, int t,  address **slave_addresses);
void* master_t(void *args);
void slave(int n, int p, int t, address *master_address, address *slave_address, char* label);

void setThreadCoreAffinity(int thread_number);
SocketConnection* connectToServer(const char* ip, int port);
SocketConnection* initializeServerSocket(const char* ip, int port);

void broadcast_vector_y(double* y, int n, address** slave_addresses, int t);
void sendData(double **matrix, int n, int start_index, int end_index, int sockfd);
mse_args_t* receiveData(SocketConnection *conn, mse_args_t *data);

void mse(mse_args_t* args);
void sendResult(int connfd, mse_args_t* data);
void receiveResult(int sockfd, double *results, int n, int start_index, int end_index);

void createMatrixVector(double** matrix, double* vector_y, int n);

double get_elapsed_time(struct timespec start, struct timespec end);
void handleError(const char* message);
void printMatrix(char *matrix_name, double **matrix, int row, int col);
void printVector(char *vector_name, double *vector, int size);
void record_experiment(char *filename, int p, int n, int t, double runtime);

////////////////////////////////////////

int main(int argc, char *argv[]) {
    srand(time(NULL));

    if (argc != 6) {
        fprintf(stderr, "❌ Error: Invalid number of arguments.\n");
        fprintf(stderr, "Usage: %s <n> <port> <status (0=master, 1=slave)> <threads> <machine_label>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    //  Read input from command line 
    int n = atoi(argv[1]);      //  n size of matrix
    int p = atoi(argv[2]);      //  port number
    int s = atoi(argv[3]);      //  status (0 = master | 1 = slave)
    int t = atoi(argv[4]);      //  number of threads
    char* machine_label = argv[5];  // machine (Laptop/PC)

    //  Print setup info
    printf("\n%s\n", DASHES DASHES DASHES DASHES);
    printf("SETUP INFORMATION\n");
    printf("— %d x %d square matrix \n— port number: %d \n— status (0 = master | 1 = slave): %d \n— %d thread/s to create \n%s\n", n, n, p, s, t, DASHES DASHES DASHES DASHES);


    //  Read IP Addresses and Port Numbers in Config File
    char filename[255];
    // sprintf(filename, "localconfig/config_%d.cfg", t);
    sprintf(filename, "droneconfig/config_%d.cfg", t);
    printf("READING FROM CONFIG FILE: %s\n", filename);

    FILE *file = fopen(filename, "r");
    if (!file) handleError("Failed to open file");


    //  Get master ip and port
    address *master_address = malloc(sizeof(address)); ;
    master_address->ip = malloc(sizeof(char) * IP_LENGTH);
    fscanf(file, "\n%[^:]:%d", master_address->ip, &master_address->port); 
    
    //  Get slaves ip and port
    int num_slaves = 0;
    fscanf(file, "%d", &num_slaves);
    printf("NUMBER OF SLAVES: %d\n", num_slaves);

    address **slave_addresses = malloc(sizeof(address) * num_slaves);
    for(int i = 0; i <  num_slaves; i++){
        address *slave_address = malloc(sizeof(address));
        slave_address->ip = malloc(sizeof(char) * IP_LENGTH);

        fscanf(file, "\n%[^:]:%d", slave_address->ip, &slave_address->port);
        printf("Slave %d  -  %s  -  %d\n", i, slave_address->ip, slave_address->port);

        slave_addresses[i] = slave_address;
    }
        printf("%s\n", DASHES DASHES DASHES DASHES);

    //  Run master process ------------------------------------------------------------------------
    if (s == 0) {
        master(n, p, t, slave_addresses);
        return 0;
    }

    //  Run slave process -------------------------------------------------------------------------
    int index = (p - slave_addresses[0]->port) % t;
    slave(n, p, t, master_address, slave_addresses[index], machine_label);

    // Cleanup
    if (s == 0) {
        for (int i = 0; i < num_slaves; i++) {
            free(slave_addresses[i]->ip);
            free(slave_addresses[i]);
        }
        free(slave_addresses);
    }

    free(master_address->ip);
    free(master_address);

    fclose(file);
} 

void master(int n, int p, int t, address **slave_addresses) {

    //  Indicate that the master is now running ---------------------------------------------------
    printf("MASTER is now LISTENING at PORT %d", p);
    printf("\n%s", DASHES DASHES DASHES DASHES);


    //  Create matrix and vector y ----------------------------------------------------------------
    double** matrix = malloc(n * sizeof(double*));
    for (int i = 0; i < n; i++) matrix[i] = malloc(n * sizeof(double));
    double* vector_y = malloc(n * sizeof(double));
    createMatrixVector(matrix, vector_y, n);

    if (n <= 15){
        printMatrix("Original Matrix (Master)", matrix, n, n);
        printVector("Original vector_y (Master)", vector_y, n);
        printf("%s\n", DASHES DASHES DASHES DASHES);
    }

    // Transpose the matrix
    double** transposed_matrix = malloc(n * sizeof(double*));
    for (int i = 0; i < n; i++) {
        transposed_matrix[i] = malloc(n * sizeof(double));
        for (int j = 0; j < n; j++) {
            transposed_matrix[i][j] = matrix[j][i];
        }
    }

    if (n <= 15) {
        printMatrix("Transposed Matrix (Each row is a column)", transposed_matrix, n, n);
    }

    //  Divide columns to t threads -----------------------------------------------------
    int work_per_thread = n / t;
    int remaining_work = n % t;
    int* starting_index_list = malloc(t * sizeof(int));
    int* ending_index_list = malloc(t * sizeof(int));
    
    for(int i = 0; i < t; i++){
        int start_index = i * work_per_thread + (i < remaining_work ? i : remaining_work);
        starting_index_list[i] = start_index;

        int end_index = start_index + work_per_thread + (i < remaining_work ? 1 : 0);
        ending_index_list[i] = end_index;
    }

    //  Divide matrix to distribute to slaves -----------------------------------------------------
    pthread_t *threads = malloc(t * sizeof(pthread_t));
    master_args_t *args = malloc(t * sizeof(master_args_t));
    double** thread_results = malloc(t * sizeof(double*)); // Allocate array of pointers for each thread's result

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    SocketConnection **connections = malloc(t * sizeof(SocketConnection*));

    for (int i = 0; i < t; i++) {
        connections[i] = connectToServer(slave_addresses[i]->ip, slave_addresses[i]->port);

        // Send vector_y broadcast
        size_t vec_bytes = n * sizeof(double), sent = 0, chunk = 4096;
        while (sent < vec_bytes) {
            ssize_t s = send(connections[i]->sockfd, ((char*)vector_y) + sent,
                             (vec_bytes - sent > chunk ? chunk : vec_bytes - sent), 0);
            if (s == -1) handleError("Failed broadcast send");
            sent += s;
        }

        args[i] = (master_args_t){
            .matrix = transposed_matrix,
            .n = n,
            .start_index = starting_index_list[i],
            .end_index = ending_index_list[i],
            .t_number = i,
            .slave_address = slave_addresses[i]
        };
        
        args[i].results = malloc((ending_index_list[i] - starting_index_list[i]) * sizeof(double));
        thread_results[i] = args[i].results;
        pthread_create(&threads[i], NULL, master_t, (void*)&args[i]);
    }


    //  Join threads ------------------------------------------------------------------------------
    for (int i = 0; i < t; i++) {
        pthread_join(threads[i], NULL);
    }

    // Aggregate results from each thread
    double *final_results = malloc(n * sizeof(double));
    for (int i = 0; i < t; i++) {
        for (int j = 0; j < (args[i].end_index - args[i].start_index); j++) {
            final_results[args[i].start_index + j] = thread_results[i][j];
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_elapsed = get_elapsed_time(start, end);
    printf("\n(Master) Time Elapsed: %f seconds\n", time_elapsed);
    printf("%s", DASHES DASHES DASHES DASHES);

    if(n <= 15){
        printVector("Collated vector e", final_results, n);
        printf("%s\n", DASHES DASHES DASHES DASHES);
    }

    record_experiment("Exer5_Master", p, n, t, time_elapsed);

    //  Free allocated memory ---------------------------------------------------------------------
    free(starting_index_list);
    free(ending_index_list);
    free(final_results);
    for (int i = 0; i < t; i++) free(args[i].results);
    free(args);
    free(threads);
    for (int i = 0; i < n; i++) free(transposed_matrix[i]);
    free(transposed_matrix);
    free(vector_y);
    for (int i = 0; i < n; i++) free(matrix[i]);
    free(matrix);
}

void* master_t(void *args) {

    //  Get args ----------------------------------------------------------------------------------
    master_args_t* actual_args = (master_args_t*)args;

    double **matrix = actual_args->matrix;
    double *results = actual_args->results;
    int n = actual_args->n;
    int start_index = actual_args->start_index;
    int end_index = actual_args->end_index;
    int t_number = actual_args->t_number;
    address *slave_address = actual_args->slave_address;


    //  Set core affinity of thread ---------------------------------------------------------------
    setThreadCoreAffinity(t_number);

    //  Create a socket for the thread ------------------------------------------------------------
    SocketConnection *conn = connectToServer(slave_address->ip, slave_address->port);
    printf("Successfully connected to %s:%d with sockfd = %d\n", slave_address->ip, slave_address->port, conn->sockfd);

    //  Send data ---------------------------------------------------------------------------------
    printf("[%d] Master sending columns %d to %d (size: %d x %d) to %s:%d\n", t_number, start_index, end_index - 1, n, end_index - start_index, slave_address->ip, slave_address->port);
    printf("%s\n", DASHES DASHES DASHES DASHES);
    sendData(matrix, n, start_index, end_index, conn->sockfd);

    //  Receive result ----------------------------------------------------------------------------
    receiveResult(conn->sockfd, results, n, start_index, end_index);
    printf("Received MSE results from %s:%d\n", slave_address->ip, slave_address->port);
    printf("%s\n", DASHES DASHES DASHES DASHES);

    // Cleanup
    close(conn->connfd);
    close(conn->sockfd);
    free(conn);
    return NULL;
}

void slave(int n, int p, int t, address *master_address, address *slave_address, char* label) {

    //  Set core affinity of slave ----------------------------------------------------------------
    setThreadCoreAffinity(slave_address->port);

    //  Create socket for slave -------------------------------------------------------------------
    printf("STARTING SLAVE %s:%d\n", slave_address->ip, slave_address->port);
    SocketConnection *conn = initializeServerSocket(slave_address->ip, slave_address->port);
    
    if(conn->sockfd == -1) handleError("Error: Server socket initialization failed.\n");
    
    printf("Slave listening at port %d\n", slave_address->port);
    printf("%s", DASHES DASHES DASHES DASHES);

    // Receive vector_y first (broadcast)
    double* y = malloc(n * sizeof(double));
    size_t received = 0, chunk = 4096;
    while (received < n * sizeof(double)) {
        ssize_t r = recv(conn->connfd, ((char*)y) + received,
                         (n * sizeof(double) - received > chunk ? chunk : n * sizeof(double) - received), 0);
        if (r <= 0) handleError("Failed receiving vector_y broadcast");
        received += r;
    }
    printf("[Slave] vector_y received (%lu bytes)\n", received);
    
    //  Receive data from master ------------------------------------------------------------------
    mse_args_t *data = malloc(sizeof(mse_args_t));
    receiveData(conn, data);
    data->y = y;

    // Start time before computation
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    //  Compute for vector e
    printf("Start computation of MSE\n");
    mse(data);
    printf("End computation of MSE\n");

    // End time after computation
    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_elapsed = get_elapsed_time(start, end);
    printf("(Slave - MSE Computation) Time Elapsed: %f seconds\n", time_elapsed);
    printf("%s", DASHES DASHES DASHES DASHES);

    char filename[64];
    snprintf(filename, sizeof(filename), "Exer5_Slave_%s", label);
    record_experiment(filename, p, n, t, time_elapsed);

    //  Send vector e to master -------------------------------------------------------------------
    sendResult(conn->connfd, data);

    // Free data and close connection -------------------------------------------------------------
    if (data->X) {
        for (int i = 0; i < (data->end_index - data->start_index); i++) {
            free(data->X[i]);
        }
        free(data->X);
    }
    free(data->y);
    free(data);

    close(conn->connfd);
    close(conn->sockfd);
    free(conn);
}

void setThreadCoreAffinity(int thread_number) {
    // Retrieve the number of online processors and calculate the count of physical cores
    int total_cores = sysconf(_SC_NPROCESSORS_ONLN);
    int physical_cores = total_cores / 2;

    // Initialize CPU set to manage processor affinity
    cpu_set_t cpu_set;
    CPU_ZERO(&cpu_set);

    // Calculate an appropriate CPU assignment for the thread
    // Aim to distribute threads across available physical cores
    int base_core = thread_number % (physical_cores - 1);
    int toggle_even_odd = (thread_number / (physical_cores - 1)) % 2;
    int cpu_to_assign = (base_core + 1) * 2 + toggle_even_odd;

    // Set the thread to run on the calculated CPU, skipping CPU 0
    CPU_SET(cpu_to_assign, &cpu_set);

    // Apply the CPU set to the current thread
    pthread_t this_thread = pthread_self();
    if (pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpu_set) != 0) handleError("Failed to set thread core affinity");
    printf("Thread %d assigned to CPU %d successfully.\n", thread_number, cpu_to_assign);
    printf("%s\n", DASHES DASHES DASHES DASHES);
}


SocketConnection* connectToServer(const char* ip, int port) {
    SocketConnection* conn = malloc(sizeof(SocketConnection));
    if (!conn) handleError("Failed to allocate memory for SocketConnection");

    // Create socket
    conn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sockfd == -1) handleError("Socket creation failed");

    // Define the server address structure
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) handleError("Invalid address / Address not supported");

    // Print actual buffer size settings
    int actual_sndbuf, actual_rcvbuf;
    socklen_t optlen = sizeof(int);
    getsockopt(conn->sockfd, SOL_SOCKET, SO_SNDBUF, &actual_sndbuf, &optlen);
    getsockopt(conn->sockfd, SOL_SOCKET, SO_RCVBUF, &actual_rcvbuf, &optlen);
    printf("[connectToServer] Actual send buffer: %d bytes, receive buffer: %d bytes\n", actual_sndbuf, actual_rcvbuf);

    // Connect to the server
    if (connect(conn->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) handleError("Connection Failed");

    return conn;
}

SocketConnection* initializeServerSocket(const char* ip, int port) {
    SocketConnection* conn = malloc(sizeof(SocketConnection));
    if (!conn) handleError("Failed to allocate memory for SocketConnection");

    // Create socket
    conn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->sockfd == -1) handleError("Socket creation failed");
    printf("Slave's socket successfully created.\n");
    
    struct sockaddr_in client_addr;
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(ip);
    server_addr.sin_port = htons(port);

    // Bind socket
    if (bind(conn->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) handleError("Failed to bind socket");
    printf("Slave's socket successfully bound.\n");
    
    
    // Listen on socket
    if (listen(conn->sockfd, 5) != 0) handleError("Failed to listen on socket");
    printf("Server socket is now listening for connections...\n");
    
    // Increase buffer sizes
    int buf_size = 2 * KB * KB;
    setsockopt(conn->sockfd, SOL_SOCKET, SO_SNDBUF, &buf_size, sizeof(buf_size));
    setsockopt(conn->sockfd, SOL_SOCKET, SO_RCVBUF, &buf_size, sizeof(buf_size));

    // Show actual buffer sizes
    int actual_sndbuf, actual_rcvbuf;
    socklen_t optlen = sizeof(int);
    getsockopt(conn->sockfd, SOL_SOCKET, SO_SNDBUF, &actual_sndbuf, &optlen);
    getsockopt(conn->sockfd, SOL_SOCKET, SO_RCVBUF, &actual_rcvbuf, &optlen);
    printf("[initializeServerSocket] Actual send buffer: %d bytes, receive buffer: %d bytes\n", actual_sndbuf, actual_rcvbuf);

    // Connect to master
    conn->addr_len = sizeof(client_addr);
    conn->connfd = accept(conn->sockfd, (struct sockaddr*)&client_addr, &conn->addr_len);
    if (conn->connfd < 0) handleError("Failed to accept connection");
    
    printf("%s\n", DASHES DASHES DASHES DASHES);
    printf("Connection accepted.\n");

    return conn;
}

void broadcast_vector_y(double* y, int n, address** slave_addresses, int t) {
    size_t vector_bytes = n * sizeof(double);
    size_t chunk_size = 4096;

    for (int i = 0; i < t; i++) {
        SocketConnection* conn = connectToServer(slave_addresses[i]->ip, slave_addresses[i]->port);
        printf("[Broadcast] Connected to %s:%d for vector_y broadcast\n", slave_addresses[i]->ip, slave_addresses[i]->port);

        size_t bytes_sent = 0;
        while (bytes_sent < vector_bytes) {
            ssize_t sent = send(conn->sockfd, ((char *)y) + bytes_sent,
                                (vector_bytes - bytes_sent > chunk_size ? chunk_size : vector_bytes - bytes_sent), 0);
            if (sent == -1) {
                perror("Failed to broadcast vector_y");
                close(conn->sockfd);
                free(conn);
                break;
            }
            bytes_sent += sent;
        }

        printf("[Broadcast] vector_y sent to %s:%d (%zu bytes)\n", slave_addresses[i]->ip, slave_addresses[i]->port, bytes_sent);
        close(conn->sockfd);
        free(conn);
    }
}


void sendData(double **matrix, int n, int start_index, int end_index, int sockfd) {
    
    //  Send matrix info --------------------------------------------------------------------------
    int matrix_info[] = {start_index, end_index, n};
    if (write(sockfd, matrix_info, sizeof(matrix_info)) <= 0) handleError("Failed to send matrix metadata");

    //  Send matrix data --------------------------------------------------------------------------
    int num_rows = end_index - start_index;
    size_t row_bytes = n * sizeof(double);
    size_t chunk_size = 4096;  // 4KB
    char *row_buffer = malloc(row_bytes);

    if (!row_buffer) handleError("Failed to allocate row buffer");

    // Send matrix rows (one row at a time in chunks)
    for (int i = 0; i < num_rows; i++) {
        memcpy(row_buffer, matrix[start_index + i], row_bytes);
        size_t bytes_sent = 0;
        while (bytes_sent < row_bytes) {
            ssize_t sent = send(sockfd, row_buffer + bytes_sent, 
                                (row_bytes - bytes_sent > chunk_size ? chunk_size : row_bytes - bytes_sent), 0);
            if (sent == -1) {
                perror("Failed to send matrix row");
                free(row_buffer);
                return;
            }
            bytes_sent += sent;
        }
    }

     printf("Matrix data sent: %d rows (%.2f KB)\n", num_rows, num_rows * row_bytes / 1024.0);
    free(row_buffer);
}

mse_args_t* receiveData(SocketConnection *conn, mse_args_t *data){
    int connfd = conn->connfd; 
    
    //  Get matrix info ---------------------------------------------------------------------------
    int matrix_info[3];
    if (read(connfd, matrix_info, sizeof(matrix_info)) <= 0) handleError("Failed to receive matrix metadata");
    
    int rows_to_receive = matrix_info[1] - matrix_info[0];
    data->n = matrix_info[2];
    data->start_index = matrix_info[0]; 
    data->end_index = matrix_info[1];

    // Allocate memory for matrix
    data->X = malloc(sizeof(double*) * rows_to_receive);
    if (data->X == NULL) handleError("Failed to allocate memory for matrix rows");

    for (int i = 0; i < rows_to_receive; i++) {
        data->X[i] = malloc(data->n* sizeof(double));
        if (data->X[i] == NULL) handleError("Failed to allocate matrix row");
    }

    //  Get matrix data ---------------------------------------------------------------------------
    size_t row_bytes = data->n* sizeof(double);
    size_t chunk_size = 4096;
    char *row_buffer = malloc(row_bytes);
    if (!row_buffer) handleError("Failed to allocate row buffer");

    for (int i = 0; i < rows_to_receive; i++) {
        size_t bytes_received = 0;
        while (bytes_received < row_bytes) {
            ssize_t r = recv(connfd, row_buffer + bytes_received,
                             (row_bytes - bytes_received > chunk_size ? chunk_size : row_bytes - bytes_received), 0);
            if (r <= 0) {
                perror("Failed to receive matrix row");
                for (int j = 0; j <= i; j++) free(data->X[j]);
                free(data->X);
                free(row_buffer);
                return NULL;
            }
            bytes_received += r;
        }
        memcpy(data->X[i], row_buffer, row_bytes);
    }
    free(row_buffer);

    printf("Successfully received matrix (%d x %d) and vector y.\n", rows_to_receive, data->n);

    if (data->n<= 15) {
        printMatrix("Received Matrix", data->X, rows_to_receive, data->n);
        printVector("Received Vector y", data->y, data->n);
        printf("%s\n", DASHES DASHES DASHES DASHES);
    }

    return data;

}

void mse(mse_args_t* args) {
    args->results = malloc(sizeof(double) * (args->end_index - args->start_index));
    
    for (int idx = args->start_index; idx < args->end_index; idx++) {
        double sum_of_squared_differences = 0;

        // Compute sum of squared differences
        for (int i = 0; i < args->n; i++) {
            double difference = args->X[idx - args->start_index][i] - args->y[i];
            sum_of_squared_differences += difference * difference;
        }

        // Compute square root of mean of sum of squared differences and assign to vector e
        args->results[idx - args->start_index] = sqrt(sum_of_squared_differences / args->n);
    }
}

void sendResult(int connfd, mse_args_t* data) {

    //  Send vector r data --------------------------------------------------------------------------
    int length = data->end_index - data->start_index;
    if (data->n <= 15) printVector("Vector e to be sent", data->results, length);

    unsigned int element_size = sizeof(double);
    int num_elements = KB / element_size;
    int groups_per_row = length / num_elements;
    int remaining_elements = length % num_elements;

    for (int i = 0; i < groups_per_row; i++) {
        int start_index = i * num_elements;
        send(connfd, &data->results[start_index], num_elements * element_size, 0);
    }
    send(connfd, &data->results[groups_per_row * num_elements], remaining_elements * element_size, 0);

    return;

}

void receiveResult(int sockfd, double *results, int n, int start_index, int end_index) {

    //  Receive vector e data -----------------------------------------------------------------------
    int length = end_index - start_index;
    unsigned int element_size = sizeof(double);
    int num_elements = KB / element_size;
    int groups_per_row = length / num_elements;
    int remaining_elements = length % num_elements;

    for(int j = 0; j < groups_per_row; j++){
        int chunk_start = j * num_elements;
        recv(sockfd, &(results[chunk_start]), num_elements * element_size, 0);
    }
    recv(sockfd, &(results[groups_per_row * num_elements]), remaining_elements * element_size, 0);

    char label[50];
    snprintf(label, sizeof(label), "Received vector e from sockfd = %d", sockfd);
    
    if (n <= 15) printVector(label, results, length);

    return;
}

void createMatrixVector(double** matrix, double* vector_y, int n){
    if (n == 15) {
        double values[15] = {68, 78, 75, 83, 80, 78, 89, 93, 90, 91, 94, 88, 84, 90, 94};
        double y_values[15] = {79.03, 79.03, 79.03, 82.11, 82.11, 82.11, 82.11, 82.11, 85.19, 85.19, 88.27, 91.35, 91.35, 91.35, 94.43};
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                matrix[i][j] = values[j]; 
            }
            vector_y[i] = y_values[i];
        }
    } else {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                matrix[i][j] = (double)(rand() % 100 + 1); 
            }
            vector_y[i] = (double)(rand() % 100 + 1);
        }
    }
}

double get_elapsed_time(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

void handleError(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void printMatrix(char *matrix_name, double **matrix, int row, int col) {
    printf("\n%s:\n", matrix_name);
    for (int i = 0; i < row; i++) {
        for (int j = 0; j < col; j++) {
            printf("%.2f\t", matrix[i][j]);
        }
        printf("\n");
    }
}

void printVector(char *vector_name, double *vector, int size) {
    printf("\n%s:\n", vector_name);
    for (int i = 0; i < size; i++) {
        printf("%.2f\t", vector[i]);
    }
    printf("\n");
}

void record_experiment(char *filename, int p, int n, int t, double runtime) {
    char tsv_filename[256];

    // Build filenames
    snprintf(tsv_filename, sizeof(tsv_filename), "util/%s.tsv", filename);

    FILE *tsv_file;

    // TSV Output
    if ((tsv_file = fopen(tsv_filename, "r")) == NULL) {
        // IF file doesn't exist, create new file then write header
        tsv_file = fopen(tsv_filename, "w");
        if (!tsv_file) {
            fprintf(stderr, "Error opening TSV file\n");
            exit(1);
        }
        fprintf(tsv_file, "Date\tPort\tn\tt\tRuntime\n");    
    } else {
        fclose(tsv_file);
        tsv_file = fopen(tsv_filename, "a");
    }

    if (!tsv_file) {
        fprintf(stderr, "Error opening output files\n");
        exit(1);
    }

    // Date/Time
    time_t now = time(NULL);
    struct tm *local_t = localtime(&now);
    char date_time[30];
    strftime(date_time, sizeof(date_time), "%m/%d/%Y %T", local_t);

    // Write/Append to TSV
    fprintf(tsv_file, "%s\t%d\t%d\t%d\t%.6f\n", date_time, p, n, t, runtime);

    fclose(tsv_file);
}