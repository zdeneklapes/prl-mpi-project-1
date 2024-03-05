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
    bool can_send = false;
    long long int numbers_count = 0;
    unsigned char *numbers = nullptr;
    int mpi_size = 0;
    std::queue<unsigned char> queues[QUEUE_COUNT];
    int mpi_send_count = 1;
    int max_queue_0_elements;
    int send_elements = 0;
    int recv_elements = 0;
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

    program->numbers_count = file_stat.st_size;
//    DEBUG_PRINT_LITE("Number count: %lld\n", program->numbers_count);
    fclose(file);
}

int get_max_mpi_size(const Program *program) {
    const int max_mpi_size = (int) (log((double) program->numbers_count) / log(2)) + 1;
    return max_mpi_size;
}

void read_numbers(Program * program) {
    FILE *file = fopen(INPUT_FILE, "rb");

    // Read
    program->numbers = (unsigned char *) malloc(program->numbers_count);
    size_t returned_code = fread(program->numbers, sizeof(program->numbers[0]), program->numbers_count, file);
    if (returned_code == 0) { // error: nothing was read
        die("Cannot read file");
    }

    if (file != nullptr) {
        fclose(file);
    }
}

int get_queue_id_send(Program * program) {
    int change = 1 << (program->process_id); // 1 << ((program->process_id + 1) - 1)
    int current_chunk = program->send_elements / change;
    int queue_id = current_chunk % QUEUE_COUNT == 0 ? Q0 : Q1;
//    DEBUG_PRINT_LITE("process_id: %d\tmpi_size: %d\tchange: %d\tcurrent_chunk: %d\tqueue_id: %d\n", program->process_id, program->mpi_size, change, current_chunk, queue_id);
    if (program->process_id == program->mpi_size - 2) {
        return 0;
    } else {
        return queue_id;
    }
}

u_char get_number_send(Program *program) {
    unsigned char number = '\0';

    DEBUG_PRINT_LITE("Q0 (size: %ld) vs. Q1 (size: %ld): %d vs %d\n", program->queues[Q0].size(), program->queues[Q1].size(), program->queues[Q0].front(), program->queues[Q1].front());
    if (program->queues[Q0].front() > program->queues[Q1].front() || program->queues[Q1].empty()) {
        number = program->queues[Q0].front();
        program->queues[Q0].pop();
    } else {
        number = program->queues[Q1].front();
        program->queues[Q1].pop();
    }
    return number;
}

void send_number(Program * program) {
    int tag = get_queue_id_send(program);
    unsigned char number = get_number_send(program);

    //    DEBUG_PRINT_LITE("START Send: %d\ttag %d\tfrom process %d\tto process %d\n", number, tag, program->process_id, program->process_id + 1);
    int returned_code = MPI_Send(&number, program->mpi_send_count, MPI_UNSIGNED_CHAR, program->process_id + 1, tag, MPI_COMM_WORLD);
    DEBUG_PRINT_LITE("Send: %d\ttag %d\tcurrent process %d\n", number, tag, program->process_id);

    if (returned_code != MPI_SUCCESS) {
        die("Cannot send data");
    } else {
        program->send_elements++;
    }
}

int get_queue_id_recv(const Program *program) {
    int change = 1 << (program->process_id - 1);
    int current_chunk = program->recv_elements / change;
    int queue_id = current_chunk % QUEUE_COUNT == 0 ? Q0 : Q1;
    if (program->process_id != program->mpi_size - 1) {
        return queue_id;
    } else {
        return 0;
    }
}


void receive_number(Program * program) {
    unsigned char number = '\0';
    MPI_Status status;
    int queue_id = get_queue_id_recv(program);
//    DEBUG_PRINT_LITE("current size Q0: %ld\tcurrent size Q1: %ld\tcurrent process %d\n", program->queues[Q0].size(), program->queues[Q1].size(), program->process_id);
    DEBUG_PRINT_LITE("Recv START\ttag %d\tcurrent process %d\n", queue_id, program->process_id);
    int returned_code = MPI_Recv(&number, program->mpi_send_count, MPI_UNSIGNED_CHAR, program->process_id - 1, queue_id,
                                 MPI_COMM_WORLD, &status);
    DEBUG_PRINT_LITE("Recv: %d\ttag %d\tcurrent process %d\n", number, queue_id, program->process_id);
    if (returned_code != MPI_SUCCESS) {
        die("Cannot receive data");
    }
    program->recv_elements++;
    program->queues[queue_id].push(number);
}

void print_output(Program * program) {
    while (program->recv_elements < program->numbers_count) {
        receive_number(program);
    }
//    DEBUG_PRINT_LITE("Q0 size: %ld\tQ1 size: %ld\tcurrent process %d\n", program->queues[Q0].size(), program->queues[Q1].size(), program->process_id);
    for (; !program->queues[Q0].empty(); program->queues[Q0].pop()) {
        std::cout << +program->queues[Q0].front() << "\n";
    }
}

void check_can_start_send(Program * program) {
    int needed_size = 1 << (program->process_id - 1);
//    DEBUG_PRINT_LITE("Needed size: %d\tcurrent size Q0: %ld\tcurrent size Q1: %ld\tcurrent process %d\n", needed_size, program->queues[Q0].size(), program->queues[Q1].size(), program->process_id);
    if (program->queues[Q0].size() >= needed_size && program->queues[Q1].size() >= 1) {
        program->can_send = true;
        DEBUG_PRINT_LITE("Can start send\tcurrent process %d\n", program->process_id);
    }
}

void print_queue(Program * program) {
    std::queue<unsigned char> q0;
    std::queue<unsigned char> q1;

    for (int i = 0; i < program->queues[Q0].size(); i++) {
        u_char number = program->queues[Q0].front();
        q0.push(number);
        program->queues[Q0].pop();
    }

    for (int i = 0; i < program->queues[Q1].size(); i++) {
        u_char number = program->queues[Q1].front();
        q1.push(number);
        program->queues[Q1].pop();
    }

    std::cout << "P" << program->process_id << " Q0 (size: " << q0.size() << "): ";
    for (int i = 0; i < q0.size(); i++) {
        u_char number = q0.front();
        std::cout << +number << " ";
        program->queues[Q0].push(number);
        q0.pop();
    }
    std::cout << "\n";

    std::cout << "P" << program->process_id << " Q1 (size: " << q1.size() << "): ";
    for (int i = 0; i < q1.size(); i++) {
        u_char number = q1.front();
        std::cout << +number << " ";
        program->queues[Q1].push(number);
        q1.pop();
    }
    std::cout << "\n";
}


void pipeline_merge_sort(Program * program) {
    if (program->process_id == 0) {
//        DEBUG_PRINT_LITE("====== First process %d ======\n", program->process_id);
        for (long long int i = program->numbers_count - 1; i >= 0; --i) {
            u_char number = program->numbers[i];
            int tag = program->numbers_count % QUEUE_COUNT == 0 ? ((i+1) % QUEUE_COUNT) : ((i) % QUEUE_COUNT);;
            if (program->mpi_size == 2) {
                tag = 0;
            }

            int returned_code = MPI_Send(&number, program->mpi_send_count, MPI_UNSIGNED_CHAR, program->process_id + 1, tag, MPI_COMM_WORLD);
            DEBUG_PRINT_LITE("Send: %d\ttag %d\tcurrent process %d\n", number, tag, program->process_id);
            if (returned_code != MPI_SUCCESS) { die("Cannot send data"); }
        }
    } else if (program->process_id != program->mpi_size - 1) {
//        DEBUG_PRINT_LITE("====== Other process %d ======\n", program->process_id);
        while (program->send_elements < program->numbers_count) {
            if (program->recv_elements < program->numbers_count) {
                receive_number(program);
            }
//            print_queue(program);

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
        for (int i = 0; i < program->numbers_count; i++) {
            std::cout << +program->numbers[i] << " ";
        }
        printf("\n");
    }

    if (program->process_id != 0) {
        usleep(10000);
    }

//    DEBUG_PRINT_LITE("Current process %d\t MPI size %d\tprogram->numbers_count %lld\n", program->process_id, program->mpi_size, program->numbers_count);
    pipeline_merge_sort(program);

    DEBUG_PRINT_LITE("Process %d finished\n", program->process_id);
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    free(program->numbers);
    program->numbers = nullptr;
    delete program;
    return 0;
}
