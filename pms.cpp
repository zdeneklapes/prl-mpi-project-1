/*
 * Parallel Merge Sort
 * Author: Zdenek Lapes <lapes.zdenek@gmail.com> (xlapes02)
 * Date: 2024-03-06
 * Description: Parallel Merge Sort using MPI
 * Compile: mpic++ -o pms pms.cpp
 * Run: mpirun -np 4 ./pms
 * Input: File "numbers" with numbers separated by space in the same directory as the executable
 * Output: Sorted numbers (stdout)
 */

#include <cstdio>
#include <mpi.h>
#include <string>
#include <sys/stat.h>
#include <deque>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <cstdarg>

#define INPUT_FILE "./numbers"
#define DEBUG 0
#define DEBUG_LITE 0
#define DEBUG_PRINT_LITE(fmt, ...) \
            do { if (DEBUG_LITE) fprintf(stderr, fmt, __VA_ARGS__); } while (0)
#define DEBUG_PRINT(fmt, ...) \
        do { if (DEBUG) fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, \
                                __LINE__, __func__, __VA_ARGS__); } while (0)

enum {
    Q0 = 0,
    Q1 = 1,
    QUEUE_COUNT = 2,
};

struct Program_s {
    int process_id = 0;
    bool can_send = false;
    long long int numbers_count = 0;
    unsigned char *numbers = nullptr;
    int mpi_size = 0;
    std::deque<unsigned char> deques[QUEUE_COUNT];
    int mpi_send_count = 1;
    int max_queue_0_elements;
    int send_elements = 0;
    int send_q0_elements = 0;
    int send_q1_elements = 0;
    int recv_elements = 0;
} typedef Program;


/**
 * File handler class
 */
class FileHandler {
public:
    FILE *file;

    // Constructor
    FileHandler(const char *filename, const char *mode) {
        file = fopen(filename, mode);
        if (file == nullptr) {
            throw std::runtime_error("File could not be opened.");
        }
    }

    // Destructor
    ~FileHandler() {
        if (file) {
            fclose(file);
            file = nullptr;
        }
    }
};

/**
 * Print error message and exit
 * @param msg
 */
void die(const char *msg, ...) {
    // Print error message with variable arguments
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fflush(stderr);
    MPI_Abort(MPI_COMM_WORLD, 1);
}

/**
 * Load file info like how many numbers are in the file
 * @param program
 */
void load_file_info(Program * program) {
    FileHandler file_handler(INPUT_FILE, "rb");

    // Get file size
    struct stat file_stat = {};
    int returned_code = fstat(fileno(file_handler.file), &file_stat);
    if (returned_code < 0) {
        die("Cannot get file size");
    }

    program->numbers_count = file_stat.st_size;
//    DEBUG_PRINT_LITE("Number count: %lld\n", program->numbers_count);
}

/**
 * Read numbers from file
 * @param program
 */
void read_numbers(Program * program) {
    FileHandler file_handler(INPUT_FILE, "rb");

    // Allocate memory for the numbers
    program->numbers = (unsigned char *) malloc(program->numbers_count);

    // Read the numbers from the file
    size_t returned_code = fread(program->numbers, sizeof(program->numbers[0]),
                                 program->numbers_count, file_handler.file);
    if (returned_code == 0) { // error: nothing was read
        die("Cannot read file");
    }
}

/**
 * Get the queue id to send to
 * @param program
 * @return
 */
int get_queue_id_send(Program * program) {
    int change = 1 << (program->process_id); // 1 << ((program->process_id + 1) - 1)
    int current_chunk = program->send_elements / change;
    int queue_id = current_chunk % QUEUE_COUNT == 0 ? Q0 : Q1;
//    DEBUG_PRINT_LITE("process_id: %d\tmpi_size: %d\tchange: %d\tcurrent_chunk: %d\tqueue_id: %d\n", program->process_id, program->mpi_size, change, current_chunk, queue_id);
    return queue_id;
}

/**
 * Get the queue id to send to
 * @param program
 * @return
 */
u_char get_number_send(Program * program) {
    unsigned char number = '\0';

    int chunk_size = 1 << (program->process_id - 1);

    int send_q0_chunks = program->send_q0_elements / chunk_size;
    int send_q1_chunks = program->send_q1_elements / chunk_size;

    DEBUG_PRINT_LITE("Q0 (size: %ld) vs. Q1 (size: %ld): %d vs %d\tcurrent process %d\n",
                     program->deques[Q0].size(), program->deques[Q1].size(),
                     program->deques[Q0].front(), program->deques[Q1].front(), program->process_id);
    if (send_q0_chunks < send_q1_chunks) {
        number = program->deques[Q0].front();
        program->deques[Q0].pop_front();
        program->send_q0_elements++;
    } else if (send_q0_chunks > send_q1_chunks) {
        number = program->deques[Q1].front();
        program->deques[Q1].pop_front();
        program->send_q1_elements++;
    } else {
        if (program->deques[Q0].front() > program->deques[Q1].front() || program->deques[Q1].empty()) {
            number = program->deques[Q0].front();
            program->deques[Q0].pop_front();
            program->send_q0_elements++;
        } else {
            number = program->deques[Q1].front();
            program->deques[Q1].pop_front();
            program->send_q1_elements++;
        }
    }

    return number;
}

/**
 * Send a number to the next process
 * @param program
 */
void send_number(Program * program) {
    int tag = get_queue_id_send(program);
    unsigned char number = get_number_send(program);

    //    DEBUG_PRINT_LITE("START Send: %d\ttag %d\tfrom process %d\tto process %d\n", number, tag, program->process_id, program->process_id + 1);
    int returned_code = MPI_Send(&number,
                                 program->mpi_send_count,
                                 MPI_UNSIGNED_CHAR,
                                 program->process_id + 1,
                                 tag,
                                 MPI_COMM_WORLD);
    DEBUG_PRINT_LITE("Send: %d \ttag %d\tcurrent process %d\n", number, tag, program->process_id);

    if (returned_code != MPI_SUCCESS) {
        die("Cannot send data");
    } else {
        program->send_elements++;
    }
}

/**
 * Get the queue id to receive from
 * @param program
 * @return
 */
int get_queue_id_recv(const Program *program) {
    int change = 1 << (program->process_id - 1);
    int current_chunk = program->recv_elements / change;
    int queue_id = current_chunk % QUEUE_COUNT == 0 ? Q0 : Q1;
    return queue_id;
}


/**
 * Receive a number from the previous process
 * @param program
 */
void receive_number(Program * program) {
    unsigned char number = '\0';
    MPI_Status status;
    int tag = get_queue_id_recv(program);
//    DEBUG_PRINT_LITE("current size Q0: %ld\tcurrent size Q1: %ld\tcurrent process %d\n", program->deques[Q0].size(), program->deques[Q1].size(), program->process_id);
    DEBUG_PRINT_LITE("Recv START\ttag %d\tcurrent process %d\n", tag, program->process_id);
    int returned_code = MPI_Recv(&number,
                                 program->mpi_send_count,
                                 MPI_UNSIGNED_CHAR,
                                 program->process_id - 1,
                                 tag,
                                 MPI_COMM_WORLD,
                                 &status);
    DEBUG_PRINT_LITE("Recv: %d\ttag %d\tcurrent process %d\n", number, tag, program->process_id);
    if (returned_code != MPI_SUCCESS) {
        die("Cannot receive data");
    }
    program->recv_elements++;
    program->deques[tag].push_back(number);
}

/**
 * Print all numbers in ascending order
 * @param program
 */
void print_output(Program * program) {
    // Wait for all numbers to be received
    while (program->recv_elements < program->numbers_count) {
        receive_number(program);
    }

//    DEBUG_PRINT_LITE("Q0 size: %ld\tQ1 size: %ld\tcurrent process %d\n", program->deques[Q0].size(), program->deques[Q1].size(), program->process_id);
    // Print all numbers in ascending order
    for (; !(program->deques[Q0].empty() && program->deques[Q1].empty());) {
        if (program->deques[Q1].empty() ||
            (!program->deques[Q0].empty() && program->deques[Q0].back() <= program->deques[Q1].back())) {
            // If Q1 is empty or the back of Q0 is smaller or equal to the back of Q1, print from Q0
            std::cout << +program->deques[Q0].back() << "\n";
            program->deques[Q0].pop_back();
        } else {
            // Otherwise, print from Q1
            std::cout << +program->deques[Q1].back() << "\n";
            program->deques[Q1].pop_back();
        }
    }
}


/**
 * Check if the process can start sending
 * @param program
 */
void check_can_start_send(Program * program) {
    int needed_size = 1 << (program->process_id - 1);
//    DEBUG_PRINT_LITE("Needed size: %d\tcurrent size Q0: %ld\tcurrent size Q1: %ld\tcurrent process %d\n", needed_size, program->deques[Q0].size(), program->deques[Q1].size(), program->process_id);
    if (program->deques[Q0].size() >= needed_size && program->deques[Q1].size() >= 1) {
        program->can_send = true;
        DEBUG_PRINT_LITE("Can start send\tcurrent process %d\n", program->process_id);
    }
}

/**
 * Merge sort using pipeline
 * @param program
 */
void pipeline_merge_sort(Program * program) {
    if (program->process_id == 0) {
//        DEBUG_PRINT_LITE("====== First process %d ======\n", program->process_id);
        // Send from last to first
        for (long long int i = program->numbers_count - 1; i >= 0; --i) {
            u_char number = program->numbers[i];

            // 1st send to q0, 2nd to q1, 3rd to q0, 4th to q1, ...
            int tag = program->numbers_count % QUEUE_COUNT == 0 ? ((i + 1) % QUEUE_COUNT) : ((i) % QUEUE_COUNT);
            int returned_code = MPI_Send(&number,
                                         program->mpi_send_count,
                                         MPI_UNSIGNED_CHAR,
                                         program->process_id + 1,
                                         tag,
                                         MPI_COMM_WORLD);
            DEBUG_PRINT_LITE("Send: %d \ttag %d\tcurrent process %d\n", number, tag, program->process_id);
            if (returned_code != MPI_SUCCESS) {
                die("Cannot send data");
            }
        }
    } else if (program->process_id != program->mpi_size - 1) {
//        DEBUG_PRINT_LITE("====== Other process %d ======\n", program->process_id);
        while (program->send_elements < program->numbers_count) {
            if (program->recv_elements < program->numbers_count) {
                receive_number(program);
            }
            if (!program->can_send) {
                check_can_start_send(program);
            }
            if (program->can_send) {
                send_number(program);
            }
        }
    } else if (program->process_id == program->mpi_size - 1) {
//        DEBUG_PRINT_LITE("====== Last process %d ======\n", program->process_id);
        print_output(program);
    }
}

int main(int argc, char *argv[]) {
    auto *program = new Program(); // Allocate memory for the program and initialize it with default values
    MPI_Init(&argc, &argv); // Initialize MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &program->process_id);
    MPI_Comm_size(MPI_COMM_WORLD, &program->mpi_size);

    // Load file info, like number count
    load_file_info(program);

    // Check if the number of processes is a power of 2 and less than the count of numbers
    program->max_queue_0_elements = (program->process_id > 0) ? 1 << (program->process_id - 1) : 0; // 2^(id-1)

    // Read numbers from file and print them
    if (program->process_id == 0) {
        read_numbers(program);
        for (int i = 0; i < program->numbers_count; i++) {
            std::cout << +program->numbers[i] << " ";
        }
        printf("\n");
    }

    // if input includes only 1 number print it and exit
    if (program->numbers_count == 1) {
        if (program->process_id == 0) {
            std::cout << +program->numbers[0] << "\n";
        }
    } else {
//    DEBUG_PRINT_LITE("Current process %d\t MPI size %d\tprogram->numbers_count %lld\n", program->process_id, program->mpi_size, program->numbers_count);
        pipeline_merge_sort(program);

        DEBUG_PRINT_LITE("Process %d finished\n", program->process_id);
        MPI_Barrier(MPI_COMM_WORLD);
    }

    MPI_Barrier(MPI_COMM_WORLD); // Wait for all processes to finish
    MPI_Finalize(); // Finalize MPI
    free(program->numbers);
    program->numbers = nullptr;
    delete program;
    return 0;
}

/* End of pms.cpp */
