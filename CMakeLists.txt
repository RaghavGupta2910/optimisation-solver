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

enable_testing()

add_executable(test_batch2
    tests/presolve/test_batch2.cpp
    src/model/model.cpp
    src/presolve/presolver.cpp
)
target_include_directories(test_batch2 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
add_test(NAME PresolveBatch2 COMMAND test_batch2)

add_executable(test_presolve_correctness
    tests/presolve/test_presolve_correctness.cpp
    src/model/model.cpp
    src/presolve/presolver.cpp
)
target_include_directories(test_presolve_correctness PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
add_test(NAME PresolveCorrectness COMMAND test_presolve_correctness)

add_executable(test_transformation_metadata
    tests/presolve/test_transformation_metadata.cpp
    src/model/model.cpp
    src/presolve/presolver.cpp
)
target_include_directories(test_transformation_metadata PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
add_test(NAME PresolveTransformationMetadata COMMAND test_transformation_metadata)

add_executable(test_pipeline_cascades
    tests/presolve/test_pipeline_cascades.cpp
    src/model/model.cpp
    src/presolve/presolver.cpp
)
target_include_directories(test_pipeline_cascades PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
add_test(NAME PresolvePipelineCascades COMMAND test_pipeline_cascades)

add_executable(test_adversarial
    tests/presolve/test_adversarial.cpp
    src/model/model.cpp
    src/presolve/presolver.cpp
)
target_include_directories(test_adversarial PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)
add_test(NAME PresolveAdversarial COMMAND test_adversarial)

# Linear programming engine. Self-contained and C++17; builds the library only.
# Its tests and benchmark harnesses stay off here and are enabled by configuring
# pdlp_engine/ standalone, or with -DPDLP_BUILD_TESTS=ON -DPDLP_BUILD_TOOLS=ON.
add_subdirectory(pdlp_engine)
