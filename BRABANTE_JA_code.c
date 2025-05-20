// Author: Jakie Ashley C. Brabante
// Section: CD-1L
// Lab Problem: 4
// Description: Distributing Parts of a Matrix over Sockets
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
    double *y;
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
void slave(int n, int p, int t, address *master_address, address *slave_address);

void setThreadCoreAffinity(int thread_number);
SocketConnection* connectToServer(const char* ip, int port);
SocketConnection* initializeServerSocket(const char* ip, int port);

void sendData(double **matrix, double *vector_y, int n, int start_index, int end_index, int sockfd);
mse_args_t* receiveData(SocketConnection *conn, mse_args_t *data);

void mse(mse_args_t* args);
void sendResult(int connfd, pearson_args_t* data);
void receiveResult(int sockfd, double *results, int n, int start_index, int end_index);

void **createMatrixVector(double** matrix, double* vector_y, int n);

double get_elapsed_time(struct timespec start, struct timespec end);
void handleError(const char* message);
void printMatrix(char *matrix_name, double **matrix, int row, int col);
void printVector(char *vector_name, double *vector, int size);
void record_experiment(char *filename, int n, int t, double runtime);

////////////////////////////////////////
// DONE
int main(int argc, char *argv[]) {

    //  Read input from command line 
    int n = atoi(argv[1]);      //  n size of matrix
    int p = atoi(argv[2]);      //  port number
    int s = atoi(argv[3]);      //  status (0 = master | 1 = slave)
    int t = atoi(argv[4]);      //  number of threads

    //  Print setup info
    printf("\n%s\n", DASHES DASHES DASHES DASHES);
    printf("SETUP INFORMATION\n");
    printf("— %d x %d square matrix \n— port number: %d \n— status (0 = master | 1 = slave): %d \n— %d thread/s to create \n%s\n", n, n, p, s, t, DASHES DASHES DASHES DASHES);


    //  Read IP Addresses and Port Numbers in Config File
    char filename[255];
    sprintf(filename, "config_%d.cfg", t);
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
    slave(n, p, t, master_address, slave_addresses[index]);

    for (int i = 0; i < num_slaves; i++) {
        free(slave_addresses[i]->ip);
        free(slave_addresses[i]);
    }
    free(slave_addresses);
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

    typedef struct {
        double **matrix;
        double *y;
        double *results;
        int n;
        int start_index; 
        int end_index;
        int t_number;
        address *slave_address;
    } master_args_t;

    for (int i = 0; i < t; i++) {
        args[i] = (master_args_t){.matrix = matrix, .y = vector_y, .n = n, .start_index = starting_index_list[i], .end_index = ending_index_list[i], .t_number = i, .slave_address = slave_addresses[i]};
        args[i].results = malloc((ending_index_list[i] - starting_index_list[i]) * sizeof(double));  // Allocate individual results array
        thread_results[i] = args[i].results; // Store pointer for aggregation

        pthread_create(&threads[i], NULL, master_t, (void *)&args[i]);
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
    printf("Time Elapsed: %f seconds\n", time_elapsed);

    if(n <= 15){
        printVector("Collated vector e", final_results, n);
        printf("%s\n", DASHES DASHES DASHES DASHES);
    }

    record_experiment("Exer4Results", n, t, time_elapsed);

    //  Free allocated memory ---------------------------------------------------------------------
    free(final_results);
    for (int i = 0; i < t; i++) free(args[i].results);
    free(args);
    free(threads);
    free(vector_y);
    for (int i = 0; i < n; i++) free(matrix[i]);
    free(matrix);
}

// void* master_t(void *args) {

//     //  Get args ----------------------------------------------------------------------------------
//     master_args_t* actual_args = (master_args_t*)args;

//     double **matrix = actual_args->matrix;
//     int n = actual_args->n;
//     int start_index = actual_args->start_index;
//     int end_index = actual_args->end_index;
//     int t_number = actual_args->t_number;
//     address *slave_address = actual_args->slave_address;


//     //  Set core affinity of thread ---------------------------------------------------------------
//     setThreadCoreAffinity(t_number);

//     //  Create a socket for the thread ------------------------------------------------------------
//     SocketConnection *conn = connectToServer(slave_address->ip, slave_address->port);
//     printf("Successfully connected to %s:%d with sockfd = %d\n", slave_address->ip, slave_address->port, conn->sockfd);

//     //  Send data ---------------------------------------------------------------------------------
//     printf("[%d] Master sending submatrix rows %d—%d to %s:%d\n", t_number, start_index, end_index, slave_address->ip, slave_address->port);
//     sendData(matrix, n, start_index, end_index, conn->sockfd);

//     //  Wait for acknowledgment from the slave ----------------------------------------------------
//     char ack[10];
//     if (recv(conn->sockfd, ack, sizeof(ack), 0) > 0) {
//         printf("Acknowledgment received from slave %s:%d\n", slave_address->ip, slave_address->port);
//     }

//     close(conn->connfd);
//     close(conn->sockfd);
//     free(conn);
//     return NULL;
// }

// void slave(int n, int p, int t, address *master_address, address *slave_address) {

//     //  Set core affinity of slave ----------------------------------------------------------------
//     setThreadCoreAffinity(slave_address->port);

//     //  Create socket for slave -------------------------------------------------------------------
//     printf("STARTING SLAVE %s:%d\n", slave_address->ip, slave_address->port);
//     SocketConnection *conn = initializeServerSocket(slave_address->ip, slave_address->port);
    
//     if(conn->sockfd == -1) handleError("Error: Server socket initialization failed.\n");
    
//     printf("Slave listening at port %d\n", slave_address->port);
//     printf("%s", DASHES DASHES DASHES DASHES);

//     // Start time before connection
//     struct timespec start, end;
//     clock_gettime(CLOCK_MONOTONIC, &start);

//     //  Receive data from master ------------------------------------------------------------------
//     data_args_t *data = malloc(sizeof(data_args_t));
//     receiveData(conn->connfd, data, slave_address->ip, slave_address->port);

//     // End time after sending ack
//     clock_gettime(CLOCK_MONOTONIC, &end);
//     double time_elapsed = get_elapsed_time(start, end);
//     printf("Time Elapsed: %f seconds\n", time_elapsed);

//     // Free data and close connection -------------------------------------------------------------
//     if (data->matrix) {
//         for (int i = 0; i < (data->end_index - data->start_index); i++) {
//             free(data->matrix[i]);
//         }
//         free(data->matrix);
//     }
//     free(data);

//     close(conn->connfd);
//     close(conn->sockfd);
//     free(conn);
// }

// void setThreadCoreAffinity(int thread_number) {
//     // Retrieve the number of online processors and calculate the count of physical cores
//     int total_cores = sysconf(_SC_NPROCESSORS_ONLN);

//     // Initialize CPU set to manage processor affinity
//     cpu_set_t cpu_set;
//     CPU_ZERO(&cpu_set);

//     // Distribute threads evenly across all available cores
//     int cpu_to_assign = thread_number % total_cores;

//     // Set the thread to run on the calculated CPU, skipping CPU 0
//     CPU_SET(cpu_to_assign, &cpu_set);

//     // Apply the CPU set to the current thread
//     pthread_t this_thread = pthread_self();
//     if (pthread_setaffinity_np(this_thread, sizeof(cpu_set_t), &cpu_set) != 0) handleError("Failed to set thread core affinity");
//     printf("Thread %d assigned to CPU %d successfully.\n", thread_number, cpu_to_assign);
    
// }

// SocketConnection* connectToServer(const char* ip, int port) {
//     SocketConnection* conn = malloc(sizeof(SocketConnection));
//     if (!conn) handleError("Failed to allocate memory for SocketConnection");

//     // Create socket
//     conn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if (conn->sockfd == -1) handleError("Socket creation failed");

//     // Define the server address structure
//     struct sockaddr_in server_addr = {0};
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_port = htons(port);
//     if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) handleError("Invalid address / Address not supported");

//     // Connect to the server
//     if (connect(conn->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) handleError("Connection Failed");

//     return conn;
// }

// SocketConnection* initializeServerSocket(const char* ip, int port) {
//     SocketConnection* conn = malloc(sizeof(SocketConnection));
//     if (!conn) handleError("Failed to allocate memory for SocketConnection");

//     // Create socket
//     conn->sockfd = socket(AF_INET, SOCK_STREAM, 0);
//     if (conn->sockfd == -1) handleError("Socket creation failed");
//     printf("Slave's socket successfully created.\n");
    
//     struct sockaddr_in client_addr;
//     struct sockaddr_in server_addr = {0};
//     server_addr.sin_family = AF_INET;
//     server_addr.sin_addr.s_addr = inet_addr(ip);
//     server_addr.sin_port = htons(port);

//     // Bind socket
//     if (bind(conn->sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) handleError("Failed to bind socket");
//     printf("Slave's socket successfully bound.\n");
    
    
//     // Listen on socket
//     if (listen(conn->sockfd, 5) != 0) handleError("Failed to listen on socket");
//     printf("Server socket is now listening for connections...\n");
    

//     conn->addr_len = sizeof(client_addr);
//     conn->connfd = accept(conn->sockfd, (struct sockaddr*)&client_addr, &conn->addr_len);
//     if (conn->connfd < 0) handleError("Failed to accept connection");
//     printf("%s\n", DASHES DASHES DASHES DASHES);
//     printf("Connection accepted.\n");
    

//     return conn;
// }

// void sendData(double **matrix, int n, int start_index, int end_index, int sockfd) {
    
//     //  Send matrix info 
//     int matrix_info[] = {start_index, end_index, n};
//     write(sockfd, matrix_info, sizeof(matrix_info));
    
//     // Send matrix data row by row
//     for (int i = start_index; i < end_index; i++) {
//         send(sockfd, matrix[i], n * sizeof(double), 0);
//     }
    
//     // Optionally send additional data or a simple confirmation message
//     char* ack = "Data Sent";
//     send(sockfd, ack, strlen(ack), 0);
// }

// mse_args_t* receiveData(SocketConnection *conn, mse_args_t *data){
//     int connfd = conn->connfd; 
    
//     //  Get matrix info ---------------------------------------------------------------------------
//     int matrix_info[3];
//     read(connfd, matrix_info, sizeof(matrix_info));
//     int rows_to_receive = matrix_info[1] - matrix_info[0];
//     data->n = matrix_info[2];
//     data->start_index = matrix_info[0]; 
//     data->end_index = matrix_info[1];

//     // Allocate memory for matrix and vector
//     data->matrix = malloc(rows_to_receive * sizeof(rows_to_receive*));
//     if (data->matrix == NULL) perror("Failed to allocate memory for matrix rows");

//     data->vector_y = malloc(sizeof(double) * data->n);
//     if (data->vector_y == NULL) handleError("Failed to allocate memory for vector y");
    
    
//     // For verification of submatrix received
//     /*
//     if (data->n <= 15) {
//         printMatrix("Received Submatrix", data->matrix, rows_to_receive, data->n);
//         printf("%s\n", DASHES DASHES DASHES DASHES);
//     }
//     */

//     // Send acknowledgment back to the master
//     send(connfd, "ack", 3, 0);
//     printf("Acknowledgment sent to master from %s:%d\n", ip, port);

// }

void **createMatrixVector(double** matrix, double* vector_y, int n){
    if (n == 15) {
        double values[15] = {68, 78, 75, 83, 80, 78, 89, 93, 90, 91, 94, 88, 84, 90, 94};
        double y_values[15] = {79.03, 79.03, 79.03, 82.11, 82.11, 82.11, 82.11, 82.11, 85.19, 85.19, 88.27, 91.35, 91.35, 91.35, 94.43};
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                matrix[j][i] = values[j]; // Automatically transposed
            }
            vector_y[i] = y_values[i];
        }
    } else {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                matrix[j][i] = (double)(rand() % 100 + 1);  // Automatically transposed
            }
            vector_y[i] = (double)(rand() % 100 + 1);
        }
    }
}

////////////////////////////////////////

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

// void record_experiment(char *filename, int n, int t, double runtime) {
//     char tsv_filename[256];
//     char pretty_filename[256];

//     // Build filenames
//     snprintf(tsv_filename, sizeof(tsv_filename), "%s.tsv", filename);
//     snprintf(pretty_filename, sizeof(pretty_filename), "%s_pretty.txt", filename);

//     FILE *tsv_file;
//     FILE *pretty_file;

//     // TSV Output
//     if ((tsv_file = fopen(tsv_filename, "r")) == NULL) {
//         // IF file doesn't exist, create new file then write header
//         tsv_file = fopen(tsv_filename, "w");
//         if (!tsv_file) {
//             fprintf(stderr, "Error opening TSV file\n");
//             exit(1);
//         }
//         fprintf(tsv_file, "Date\tn\tt\tRuntime\n");    
//     } else {
//         fclose(tsv_file);
//         tsv_file = fopen(tsv_filename, "a");
//     }

//     // Pretty Output
//     if ((pretty_file = fopen(pretty_filename, "r")) == NULL) {
//         // IF file doesn't exist, create new file then write header
//         pretty_file = fopen(pretty_filename, "w");
//         if (!pretty_file) {
//             fprintf(stderr, "Error opening Pretty file\n");
//             exit(1);
//         }
//         fprintf(pretty_file, "| %-19s | %-10s | %-10s | %-10s |\n", "Date/Time", "n", "t", "Runtime");
//         fprintf(pretty_file, "+---------------------+---------------+------------+------------+------------+\n");
//     } else {
//         fclose(pretty_file);
//         pretty_file = fopen(pretty_filename, "a");
//     }

//     if (!tsv_file || !pretty_file) {
//         fprintf(stderr, "Error opening output files\n");
//         exit(1);
//     }

//     // Date/Time
//     time_t now = time(NULL);
//     struct tm *local_t = localtime(&now);
//     char date_time[30];
//     strftime(date_time, sizeof(date_time), "%m/%d/%Y %T", local_t);

//     // Write/Append to TSV
//     fprintf(tsv_file, "%s\t%d\t%d\t%.6f\n", date_time, n, t, runtime);

//     // Write/Append to Pretty
//     fprintf(pretty_file, "| %-19s | %-10d | %-10d | %-10.6f |\n", date_time, n, t, runtime);

//     fclose(tsv_file);
//     fclose(pretty_file);
// }