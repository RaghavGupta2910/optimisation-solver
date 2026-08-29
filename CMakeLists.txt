cmake_minimum_required(VERSION 3.20)

project(Optimisation_solver)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(bench
    src/main.cpp
    src/model/model.cpp
    src/benchmark/benchmarkresult.cpp
    src/presolve/presolver.cpp
)

target_include_directories(bench PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)