#pragma once

#include "benchmark/benchmarkresult.h"
#include "benchmark/instancemanager.h"

#include <chrono>
#include <string>
#include <vector>

namespace benchmark {

// ============================================================
// ProcessResult
// ============================================================

struct ProcessResult {

    int exitCode = -1;

    std::string stdoutOutput;
    std::string stderrOutput;

    std::chrono::milliseconds runtime{0};

    bool timedOut = false;
    bool launchFailed = false;

    long long peakMemoryKB = -1;
    bool memoryLimitExceeded = false;
};

// ============================================================
// ProcessManager
// ============================================================

class ProcessManager {

public:

    ProcessResult run(
        const std::string& executable,
        const std::vector<std::string>& arguments,
        std::chrono::milliseconds timeout,
        long long memoryLimitKB = -1
    );

};

// ============================================================
// runBenchmark
//
// Convenience wrapper: runs the solver via ProcessManager and
// immediately converts the raw ProcessResult into a structured
// BenchmarkResult. Declared here so other files (e.g.
// solvercomparison.h) can call it — the actual definition lives
// in benchmark_model/benchmarkcode.cpp.
// ============================================================

BenchmarkResult runBenchmark(
    ProcessManager& manager,
    const std::string& solverName,
    const std::string& instanceName,
    const std::string& executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout,
    long long memoryLimitKB = -1
);

std::vector<BenchmarkResult> runBenchmarkSuite(
    ProcessManager& manager,
    const std::string& solverName,
    const std::vector<BenchmarkInstance>& instances,
    const std::string& executable,
    std::chrono::milliseconds timeout,
    long long memoryLimitKB = -1
);

} // namespace benchmark