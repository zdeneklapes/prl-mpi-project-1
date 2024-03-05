#include <cstdio>
#include <mpi.h>
#include <string>
#include <sys/stat.h>
#include <queue>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <cmath>
#include <cassert>
#include <cstdarg>

#define INPUT_FILE "./numbers"
#define DEBUG 1
#define DEBUG_LITE 1
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
    bool started_send = false;
    long long int number_count = 0;
    unsigned char *numbers = nullptr;
    int mpi_size = 0;
    std::queue<unsigned char> queues[QUEUE_COUNT] = {};
    int mpi_send_count = 1;
    int max_queue_0_elements;
    int processed_elements = 0;
} typedef Program;


void die(const char *msg, ...) {
    // Print error message with variable arguments
    va_list args;
    va_start(args, msg);
    vfprintf(stderr, msg, args);
    va_end(args);
    fflush(stderr);
    MPI_Abort(MPI_COMM_WORLD, 1);
}

void load_file_info(Program * program) {
    FILE *file = fopen(INPUT_FILE, "rb");
    if (file == nullptr) {
        die("Cannot open file");
    }

    // Get file size
    struct stat file_stat = {};
    int returned_code = fstat(fileno(file), &file_stat);
    if (returned_code < 0) {
        die("Cannot get file size");
    }

    program->number_count = file_stat.st_size;
//    DEBUG_PRINT_LITE("Number count: %lld\n", program->number_count);
    fclose(file);
}

int get_max_mpi_size(const Program *program) {
    const int max_mpi_size = (int) (log((double) program->number_count) / log(2)) + 1;
    return max_mpi_size;
}

void read_numbers(Program * program) {
    FILE *file = fopen(INPUT_FILE, "rb");

    // Read
    program->numbers = (unsigned char *) malloc(program->number_count);
    size_t returned_code = fread(program->numbers, sizeof(program->numbers[0]), program->number_count, file);
    if (returned_code == 0) { // error: nothing was read
        die("Cannot read file");
    }

    if (file != nullptr) {
        fclose(file);
    }
}

int get_queue_id_send(Program * program) {
    /*
     * P1 sending 2 -> q0,q0,q1,q1,q0,q0,q1,q1,...
     * 2^{1-1} = 2^0 = 1 -> 2
     *
     * P0
     * 2^{0-1} = 2^{-1} = 0 -> 1
     * */
    int change = 1 << ((program->process_id + 1) - 1);
    int foo = program->processed_elements / change;
    return foo % QUEUE_COUNT;
}

int get_queue_id_recv(const int process_id, const int received_elements) {
    /*
     * P1 sending 2 -> q0,q0,q1,q1,q0,q0,q1,q1,...
     * 2^{1} = 2^0 = 1
     *
     * P0
     * 2^{0-1} = 2^{-1} = 0
     * */
    int change = 1 << (process_id - 1);
    int foo = (received_elements) / change;
    return foo % QUEUE_COUNT;
}

void send_number(const unsigned char number, Program *program) {
//    DEBUG_PRINT("Sending number: %d\t from process %d\tto process %d\twith tag %d\n",
//                number, program->process_id, program->process_id + 1, queue_id);
    int queue_id = get_queue_id_send(program);
    int returned_code = MPI_Send(&number, program->mpi_send_count, MPI_UNSIGNED_CHAR,
                                 program->process_id + 1, queue_id, MPI_COMM_WORLD);
    program->processed_elements++;
    DEBUG_PRINT_LITE("Send: %d\ttag %d\tfrom process %d\tto process %d\n",
                     number, queue_id, program->process_id, program->process_id + 1);
    if (returned_code != MPI_SUCCESS) {
        die("Cannot send data");
    }
}

void process_input(Program * program) {
    int returned_code = 0;
    for (long long int i = program->number_count - 1; i >= 0; --i) {
        // 1.option: 11 % 2 = 1 -> 0; 10 % 2 = 0 -> 1
        // 2.option: 10 % 2 = 0 -> 0; 9 % 2 = 1 -> 1
        int queue_id = (program->number_count % 2 != 0) ? ((int) i) % QUEUE_COUNT : ((int) (i + 1)) % QUEUE_COUNT;
        send_number(program->numbers[i], program);
    }
}

void receive_number(const int need_elements, Program *program) {
    unsigned char number = '\0';
    MPI_Status status;
    int queue_id = get_queue_id_recv(program->process_id, program->processed_elements);
//    DEBUG_PRINT_LITE("START Recv\ttag %d\tfrom process %d\tto process %d\n", queue_id, program->process_id - 1, program->process_id);
    int returned_code = MPI_Recv(&number, program->mpi_send_count, MPI_UNSIGNED_CHAR, program->process_id - 1, queue_id, MPI_COMM_WORLD, &status);
    DEBUG_PRINT_LITE("Recv: %d\ttag %d\tfrom process %d\tto process %d\n", number, queue_id, program->process_id - 1, program->process_id);
    if (returned_code != MPI_SUCCESS) {
        die("Cannot receive data");
    } else {
        program->queues[queue_id].push(number);
    }
}

void merge_queues(Program * program) {
    // TODO: Merge 2 queues and pass them to the next process
    while (!(program->queues[Q0].empty() && program->queues[Q1].empty())) {
        const u_char q0_number = program->queues[Q0].front();
        const u_char q1_number = program->queues[Q1].front();

        if ((q0_number > q1_number) || program->queues[Q1].empty()) {
            send_number(q0_number, program);
            program->queues[Q0].pop();
        } else {
            send_number(q1_number, program);
            program->queues[Q1].pop();
        }
    }
    assert(program->queues[Q0].empty() && program->queues[Q1].empty()); // Both queues should be empty
}

void process_output(Program * program) {
    // TODO: print all the numbers
    receive_number(program->number_count, program);
    for (int i = 0; i < program->number_count; i++) {
        std::cout << +program->numbers[i] << " ";
    }
}

void process_id_1(Program * program) {
    if (program->number_count == 2) {
        process_output(program);
    }

    while (program->processed_elements < program->number_count) {
        //
        receive_number(1, program); // Q0 needs at least 1 element

        //
        int queue_id = get_queue_id_send(program);
        int number = program->queues[Q0].front();
        int returned_code = MPI_Send(&number, program->mpi_send_count, MPI_UNSIGNED_CHAR,
                                     program->process_id + 1, queue_id, MPI_COMM_WORLD);

        if (returned_code != MPI_SUCCESS) {
            die("Cannot send data");
        }
    }
}

void process_middle(Program * program) {
    // TODO: Load queues from the previous process
//    DEBUG_PRINT("Process %d\tmax_queue_0_elements: %d\tprocessed_elements: %d\tnumber_count: %lld\n",
//                program->process_id, program->max_queue_0_elements, program->processed_elements, program->number_count);
    while (program->processed_elements < program->number_count) {
        const int q0_need_elements = program->max_queue_0_elements - program->queues[Q0].size();
        DEBUG_PRINT_LITE("Process %d\tneed_elements: %d\n", program->process_id, q0_need_elements);
        receive_number(q0_need_elements, program);
        receive_number(1, program); // Q1 needs at least 1 element

        if (program->queues[Q0].size() == program->max_queue_0_elements && (int) program->queues[Q1].size() >= 1) {
            merge_queues(program);
        }
    }
}

bool can_start_send(Program * program) {
    int needed_size = 1 << (program->process_id - 1);
    if (program->queues[Q0].size() >= needed_size && program->queues[Q1].size() >= 1) {
        return true;
    } else {
        return false;
    }
}

void pipeline_merge_sort(Program * program) {
    if (program->process_id == 0) {
        process_input(program);
    } else if (program->process_id != program->mpi_size - 1) {
        while (true) {
            receive_number(1, program); // Q0 needs at least 1 element

            if (can_start_send(program) == true) {
                program->started_send = true;
            }

            if (program->started_send == true) {
                if (program->queues[Q0].front() > program->queues[Q1].front()) {
                    send_number(program->queues[Q1].front(), program);
                } else {
                    send_number(program->queues[Q0].front(), program);
                }
            }
        }
    } else { // program->process_id == program->mpi_size
        process_output(program);
    }
}

int main(int argc, char *argv[]) {
    auto *program = new Program(); // Allocate memory for the program and initialize it with default values
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &program->process_id);
    MPI_Comm_size(MPI_COMM_WORLD, &program->mpi_size);

    // Load file info, like number count
    load_file_info(program);

    // Check if the number of processes is a power of 2 and less than the count of numbers
    program->max_queue_0_elements = (program->process_id > 0) ? 1 << (program->process_id - 1) : 0; // 2^(id-1)
    assert(program->mpi_size == get_max_mpi_size(program));

    if (program->process_id == 0) {
        read_numbers(program);
        for (int i = 0; i < program->number_count; i++) {
            std::cout << +program->numbers[i] << " ";
        }
        printf("\n");
    }

    if (program->process_id != 0) {
        usleep(10000);
    }
    pipeline_merge_sort(program);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    free(program->numbers);
    program->numbers = nullptr;
    delete program;
    return 0;
}
