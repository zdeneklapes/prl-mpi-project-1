project(ParallelComputing)
cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenMP REQUIRED)
find_package(MPI REQUIRED)

include_directories(${MPI_INCLUDE_PATH})

add_executable(ParallelComputing pms.cpp)
target_link_libraries(ParallelComputing OpenMP::OpenMP_CXX ${MPI_LIBRARIES})
