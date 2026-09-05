#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <sstream>
#include <vector>

namespace benchmark {

// ============================================================
// SuccessStatus
// ============================================================

enum class SuccessStatus {
    Success,       // exited 0, didn't time out, didn't fail to launch
    NonZeroExit,   // ran to completion but returned a non-zero code
    TimedOut,
    LaunchFailed,
    Unknown
};

inline std::string toString(SuccessStatus status) {

    switch (status) {

        case SuccessStatus::Success:      return "SUCCESS";
        case SuccessStatus::NonZeroExit:  return "NON_ZERO_EXIT";
        case SuccessStatus::TimedOut:     return "TIMED_OUT";
        case SuccessStatus::LaunchFailed: return "LAUNCH_FAILED";
        default:                          return "UNKNOWN";
    }
}

// ============================================================
// SolverStatistics
//
// Extension point for solver-specific parsing. Everything is
// optional and unset for now — no parsing happens in this layer.
// ============================================================

struct SolverStatistics {

    std::optional<double> objectiveValue;

    std::optional<std::string> primalStatus;
    std::optional<std::string> dualStatus;

    std::optional<long long> iterations;
    std::optional<long long> peakMemoryKB;

    std::optional<long long> presolveRowsRemoved;
    std::optional<long long> presolveColsRemoved;
    std::optional<double> presolveTimeMs;
};

// ============================================================
// BenchmarkResult
// ============================================================

struct BenchmarkResult {

    std::string solverName;
    std::string instanceName;

    int exitCode = -1;
    bool timedOut = false;
    bool launchFailed = false;
    std::chrono::milliseconds runtime{0};

    std::string stdoutOutput;
    std::string stderrOutput;

    SuccessStatus status = SuccessStatus::Unknown;

    // populated from ProcessResult.peakMemoryKB when available
    std::optional<long long> peakMemoryKB;

    // true if the process appears to have been killed for
    // exceeding a memory limit (best-effort heuristic)
    bool memoryLimitExceeded = false;

    SolverStatistics stats;

    bool isSuccess() const {
        return status == SuccessStatus::Success;
    }
};

// ============================================================
// buildBenchmarkResult
//
// Converts a ProcessResult-shaped object into a BenchmarkResult.
// Requires the object to expose: .exitCode, .timedOut,
// .launchFailed, .runtime, .stdoutOutput, .stderrOutput,
// .peakMemoryKB, .memoryLimitExceeded
// ============================================================

template <typename ProcessResultT>
inline BenchmarkResult buildBenchmarkResult(
    const std::string& solverName,
    const std::string& instanceName,
    const ProcessResultT& processResult
) {
    BenchmarkResult result;

    result.solverName = solverName;
    result.instanceName = instanceName;

    result.exitCode = processResult.exitCode;
    result.timedOut = processResult.timedOut;
    result.launchFailed = processResult.launchFailed;
    result.runtime = processResult.runtime;

    result.stdoutOutput = processResult.stdoutOutput;
    result.stderrOutput = processResult.stderrOutput;

    if (processResult.peakMemoryKB >= 0) {
        result.peakMemoryKB = processResult.peakMemoryKB;
    }

    result.memoryLimitExceeded = processResult.memoryLimitExceeded;

    if (processResult.launchFailed) {
        result.status = SuccessStatus::LaunchFailed;
    } else if (processResult.timedOut) {
        result.status = SuccessStatus::TimedOut;
    } else if (processResult.exitCode == 0) {
        result.status = SuccessStatus::Success;
    } else {
        result.status = SuccessStatus::NonZeroExit;
    }

    return result;
}

// ============================================================
// summarize
// ============================================================

inline std::string summarize(const BenchmarkResult& result) {

    std::ostringstream out;

    out << result.solverName << " | " << result.instanceName
        << " | status=" << toString(result.status)
        << " | exitCode=" << result.exitCode
        << " | runtime_ms=" << result.runtime.count();

    if (result.peakMemoryKB.has_value()) {
        out << " | peakMemKB=" << result.peakMemoryKB.value();
    }

    if (result.memoryLimitExceeded) {
        out << " | MEMORY_LIMIT_EXCEEDED";
    }

    return out.str();
}

// ============================================================
// AggregateMetrics
//
// Summary statistics across multiple BenchmarkResults, including
// percentiles — the Runtime Metrics layer.
// ============================================================

struct AggregateMetrics {
    int totalRuns = 0;
    int successCount = 0;
    int timeoutCount = 0;
    int failureCount = 0;

    std::chrono::milliseconds totalRuntime{0};
    std::chrono::milliseconds minRuntime{0};
    std::chrono::milliseconds maxRuntime{0};
    std::chrono::milliseconds averageRuntime{0};

    std::chrono::milliseconds p50Runtime{0};
    std::chrono::milliseconds p90Runtime{0};
    std::chrono::milliseconds p99Runtime{0};
};

namespace detail {

    inline std::chrono::milliseconds percentile(
        const std::vector<std::chrono::milliseconds>& sortedRuntimes,
        double p
    ) {
        if (sortedRuntimes.empty()) {
            return std::chrono::milliseconds(0);
        }

        double rank = p * (static_cast<double>(sortedRuntimes.size()) - 1.0);

        size_t lowerIndex = static_cast<size_t>(rank);
        size_t upperIndex =
            (lowerIndex + 1 < sortedRuntimes.size()) ? lowerIndex + 1 : lowerIndex;

        double fraction = rank - static_cast<double>(lowerIndex);

        double interpolated =
            static_cast<double>(sortedRuntimes[lowerIndex].count()) +
            fraction *
                static_cast<double>(
                    sortedRuntimes[upperIndex].count() -
                    sortedRuntimes[lowerIndex].count()
                );

        return std::chrono::milliseconds(
            static_cast<long long>(interpolated)
        );
    }

} // namespace detail

inline AggregateMetrics computeAggregateMetrics(
    const std::vector<BenchmarkResult>& results
) {
    AggregateMetrics metrics;
    metrics.totalRuns = static_cast<int>(results.size());

    std::vector<std::chrono::milliseconds> runtimes;
    runtimes.reserve(results.size());

    bool first = true;

    for (const auto& r : results) {

        if (r.status == SuccessStatus::Success) {
            metrics.successCount++;
        } else if (r.status == SuccessStatus::TimedOut) {
            metrics.timeoutCount++;
        } else if (
            r.status == SuccessStatus::NonZeroExit ||
            r.status == SuccessStatus::LaunchFailed
        ) {
            metrics.failureCount++;
        }

        metrics.totalRuntime += r.runtime;
        runtimes.push_back(r.runtime);

        if (first) {
            metrics.minRuntime = r.runtime;
            metrics.maxRuntime = r.runtime;
            first = false;
        } else {
            if (r.runtime < metrics.minRuntime) metrics.minRuntime = r.runtime;
            if (r.runtime > metrics.maxRuntime) metrics.maxRuntime = r.runtime;
        }
    }

    if (metrics.totalRuns > 0) {
        metrics.averageRuntime = std::chrono::milliseconds(
            metrics.totalRuntime.count() / metrics.totalRuns
        );
    }

    std::sort(runtimes.begin(), runtimes.end());

    metrics.p50Runtime = detail::percentile(runtimes, 0.50);
    metrics.p90Runtime = detail::percentile(runtimes, 0.90);
    metrics.p99Runtime = detail::percentile(runtimes, 0.99);

    return metrics;
}

// ============================================================
// groupBySolver / groupByInstance
//
// Break a batch of results into per-solver / per-instance
// AggregateMetrics — lets you compare solvers or spot which
// instances are consistently slow.
// ============================================================

inline std::map<std::string, AggregateMetrics> groupBySolver(
    const std::vector<BenchmarkResult>& results
) {
    std::map<std::string, std::vector<BenchmarkResult>> grouped;

    for (const auto& r : results) {
        grouped[r.solverName].push_back(r);
    }

    std::map<std::string, AggregateMetrics> output;

    for (const auto& [solverName, group] : grouped) {
        output[solverName] = computeAggregateMetrics(group);
    }

    return output;
}

inline std::map<std::string, AggregateMetrics> groupByInstance(
    const std::vector<BenchmarkResult>& results
) {
    std::map<std::string, std::vector<BenchmarkResult>> grouped;

    for (const auto& r : results) {
        grouped[r.instanceName].push_back(r);
    }

    std::map<std::string, AggregateMetrics> output;

    for (const auto& [instanceName, group] : grouped) {
        output[instanceName] = computeAggregateMetrics(group);
    }

    return output;
}

// ============================================================
// summarize (AggregateMetrics overload)
// ============================================================

inline std::string summarize(const AggregateMetrics& metrics) {

    std::ostringstream out;

    out << "runs=" << metrics.totalRuns
        << " | success=" << metrics.successCount
        << " | timeout=" << metrics.timeoutCount
        << " | failed=" << metrics.failureCount
        << " | avg_ms=" << metrics.averageRuntime.count()
        << " | min_ms=" << metrics.minRuntime.count()
        << " | max_ms=" << metrics.maxRuntime.count()
        << " | p50_ms=" << metrics.p50Runtime.count()
        << " | p90_ms=" << metrics.p90Runtime.count()
        << " | p99_ms=" << metrics.p99Runtime.count();

    return out.str();
}

} // namespace benchmark