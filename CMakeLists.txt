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
# Linear programming engine. Self-contained and C++17; builds the library only.
# Its tests and benchmark harnesses stay off here and are enabled by configuring
# pdlp_engine/ standalone, or with -DPDLP_BUILD_TESTS=ON -DPDLP_BUILD_TOOLS=ON.
add_subdirectory(pdlp_engine)
