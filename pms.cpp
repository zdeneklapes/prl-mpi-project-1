//
// Created by Zdeněk Lapeš on 02/03/2024.
//

#include <stdio.h>
#include <mpi.h>

void die() {
    printf("Error starting MPI program. Terminating.\n");
    MPI_Abort(MPI_COMM_WORLD, 1);
}

int main(int argc, char *argv[]) {
    int num_procs, process_id;
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) { die(); }
    if (MPI_Comm_rank(MPI_COMM_WORLD, &process_id) != MPI_SUCCESS) { die(); }
    if (MPI_Comm_size(MPI_COMM_WORLD, &num_procs) != MPI_SUCCESS) { die(); }
    printf("Process %d of %d\n", process_id, num_procs);
    if (MPI_Finalize() != MPI_SUCCESS) { die(); }
    return 0;
}
