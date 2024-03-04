#include <cstdio>
#include <mpi.h>
#include <string>
#include <sys/stat.h>
#include <queue>
#include <filesystem>
#include <iostream>
#include <unistd.h>
#include <cmath>

#define INPUT_FILE "./numbers"

enum {
    Q0 = 0,
    Q1 = 1,
    QUEUE_COUNT = 2,
};

struct Program {
    int process_id = 0;
    long long int number_count = 0;
    unsigned char *numbers = nullptr;
    int mpi_size = 0;
    std::queue<unsigned char> queues[QUEUE_COUNT] = {};
};


void die(const char *msg) {
    printf("Error: %s\n", msg);
    printf("Terminating...\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
}

void errhandler_func(MPI_Comm *comm, int *err, ...) {
    char err_string[MPI_MAX_ERROR_STRING];
    int resultlen;
    MPI_Error_string(*err, err_string, &resultlen);
    die(err_string);
}

void read_numbers(Program *program) {
    FILE *file = fopen(INPUT_FILE, "rb");
    int returned_code = 0;
    if (file == nullptr) {
        die("Cannot open file");
    }

    // Get file size
    struct stat file_stat;
    returned_code = fstat(fileno(file), &file_stat);
    if (returned_code < 0) {
        die("Cannot get file size");
    }
    program->number_count = file_stat.st_size;

    // Check if the number of processes is a power of 2 and less than the count of numbers
    const int max_mpi_size = (int) (log(program->number_count) / log(2)) + 1;
    if (max_mpi_size == program->mpi_size) {
        die("Number of processes must be a power of 2");
    }

    // Read
    program->numbers = (unsigned char *) malloc(program->number_count);
    returned_code = fread(program->numbers, sizeof(program->numbers[0]), (int) program->number_count, file);
    if (returned_code < 0) {
        die("Cannot read file");
    }

    if (file != nullptr) {
        fclose(file);
    }
}

void pipeline_merge_sort(struct Program *program) {
    int returned_code = 0;
    int send_amount = 1;
    if (program->process_id == 0) {
        for (long long int i = program->number_count - 1; i >= 0; --i) {
            int proc_id = program->process_id + 1;
            u_char *number = &program->numbers[i];
            returned_code = MPI_Send(number, send_amount, MPI_UNSIGNED_CHAR,
                                     proc_id, (int) (i % QUEUE_COUNT), MPI_COMM_WORLD);
        }
        if (returned_code != MPI_SUCCESS) {
            die("Cannot send data");
        }
    } else {
        int max_queue_lenght = 1 << (program->process_id - 1); // 2^(id-1)

        // fill the Q0
        if (program->queues[Q0].size() < max_queue_lenght) {
            for (int i = 0; i < max_queue_lenght; i++) {
                MPI_Status status;
                unsigned char number = '\0';
                returned_code = MPI_Recv(&number, send_amount, MPI_UNSIGNED_CHAR, program->process_id - 1,
                                         0, MPI_COMM_WORLD, &status);
                if (returned_code != MPI_SUCCESS) {
                    die("Cannot receive data");
                }
            }
        }

        // Q1 at least 1 element
        if (program->queues[Q0].empty()) {
            MPI_Status status;
            unsigned char number = '\0';
            returned_code = MPI_Recv(&number, send_amount, MPI_UNSIGNED_CHAR, program->process_id - 1,
                                     0, MPI_COMM_WORLD, &status);
            if (returned_code != MPI_SUCCESS) {
                die("Cannot receive data");
            }
            program->queues[Q1].push(number);
        }


        // pass to the next process
        const int q1_front_number = program->queues[Q1].front();


        // send the last number to the previous process
        // TODO: Input -> P1 go in order q0,q1,q0,q1,q0,q1,q0,q1
        // TODO: P1 -> P2 items go in order that P2 handles the order dynamically if item is smaller than q0_front_number
        // then goes to q0, if it is bigger then q0_back_number then goes to q1
        // TODO [QUESTION]: what is item is between q0_front_number and q0_back_number???


        MPI_Status status;
        unsigned char number = '\0';
        returned_code = MPI_Recv(&number, send_amount, MPI_UNSIGNED_CHAR, program->process_id - 1,
                                 0, MPI_COMM_WORLD, &status);
        if (returned_code != MPI_SUCCESS) {
            die("Cannot receive data");
        }

    }
}

int main(int argc, char *argv[]) {
    auto *program = new Program(); // Allocate memory for the program and initialize it with default values
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &program->process_id);
    MPI_Comm_size(MPI_COMM_WORLD, &program->mpi_size);

    if (program->process_id == 0) {
        read_numbers(program);
        printf("Numbers: ddd\n");
        for (int i = 0; i < program->number_count; i++) {
            std::cout << +program->numbers[i] << " ";
        }
    } else {
        printf("id: %d\n", program->process_id);
    }

    if (program->process_id != 0) usleep(10000);

    pipeline_merge_sort(program);

    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    free(program->numbers);
    program->numbers = nullptr;
    delete program;
    return 0;
}
