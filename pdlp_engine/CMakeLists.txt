cmake_minimum_required(VERSION 3.20)
project(pdlp_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Without this, a plain `cmake -S . -B build` produces an unoptimised binary.
# For this engine that is roughly a 20x slowdown, which is easy to mistake for
# the solver being slow.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set; defaulting to Release")
endif()

option(PDLP_NATIVE_ARCH "Tune for the building machine's CPU" ON)
option(PDLP_ENABLE_LTO "Enable link-time optimisation for Release builds" ON)

add_library(pdlp_engine
    src/sparse_matrix.cpp
    src/parallel.cpp
    src/scaling.cpp
    src/anderson.cpp
    src/preconditioner.cpp
    src/step_controller.cpp
    src/iterate_average.cpp
    src/restart_controller.cpp
    src/termination.cpp
    src/pdhg_kernel.cpp
    src/feasibility_polishing.cpp
    src/pdlp_solver.cpp
)

target_include_directories(pdlp_engine PUBLIC include)

find_package(Threads REQUIRED)
target_link_libraries(pdlp_engine PUBLIC Threads::Threads)

include(CheckCXXCompilerFlag)

if(MSVC)
    target_compile_options(pdlp_engine PRIVATE /W4 /permissive- /fp:precise)
    target_compile_options(pdlp_engine PRIVATE $<$<CONFIG:Release>:/O2>)
else()
    target_compile_options(pdlp_engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(pdlp_engine PRIVATE $<$<CONFIG:Release>:-O3>)

    # Contraction into FMA is safe here and shortens the sparse dot products.
    # Full -ffast-math is deliberately NOT used: it permits reassociating the
    # residual reductions and would let the compiler assume no infinities, which
    # this code relies on for one-sided row bounds.
    check_cxx_compiler_flag("-ffp-contract=fast" PDLP_HAS_FP_CONTRACT)
    if(PDLP_HAS_FP_CONTRACT)
        target_compile_options(pdlp_engine PRIVATE -ffp-contract=fast)
    endif()

    if(PDLP_NATIVE_ARCH)
        check_cxx_compiler_flag("-march=native" PDLP_HAS_MARCH_NATIVE)
        if(PDLP_HAS_MARCH_NATIVE)
            target_compile_options(pdlp_engine PUBLIC -march=native)
        else()
            check_cxx_compiler_flag("-mcpu=native" PDLP_HAS_MCPU_NATIVE)
            if(PDLP_HAS_MCPU_NATIVE)
                target_compile_options(pdlp_engine PUBLIC -mcpu=native)
            endif()
        endif()
    endif()
endif()

if(PDLP_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT PDLP_IPO_SUPPORTED OUTPUT PDLP_IPO_MESSAGE)
    if(PDLP_IPO_SUPPORTED)
        set_property(TARGET pdlp_engine
            PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(STATUS "LTO unavailable: ${PDLP_IPO_MESSAGE}")
    endif()
endif()

add_executable(pdlp_example examples/solve_example.cpp)
target_link_libraries(pdlp_example PRIVATE pdlp_engine)

add_executable(pdlp_bench bench/bench_pdlp.cpp)
target_link_libraries(pdlp_bench PRIVATE pdlp_engine)

add_executable(pdlp_validate tools/validate_against_reference.cpp)
target_link_libraries(pdlp_validate PRIVATE pdlp_engine)

add_executable(pdlp_ablation tools/ablation.cpp)
target_link_libraries(pdlp_ablation PRIVATE pdlp_engine)

enable_testing()
add_executable(pdlp_tests tests/test_pdlp.cpp)
target_link_libraries(pdlp_tests PRIVATE pdlp_engine)
add_test(NAME pdlp_tests COMMAND pdlp_tests)
