#pragma once

#include "benchmark/benchmarkresult.h"
#include "benchmark/instancemanager.h"
#include "benchmark/processmanager.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace benchmark {

// ============================================================
// SolverSpec
//
// One solver under comparison: a display name plus the
// executable used to run it.
// ============================================================

struct SolverSpec {
    std::string name;
    std::string executablePath;
};

// ============================================================
// InstanceComparisonRow
//
// How every solver performed on ONE specific instance.
// ============================================================

struct InstanceComparisonRow {

    std::string instanceName;

    // solverName -> its BenchmarkResult on this instance
    std::map<std::string, BenchmarkResult> resultsBySolver;

    // name of the fastest solver that succeeded on this instance,
    // if any did
    std::optional<std::string> fastestSolver;
};

// ============================================================
// SolverComparisonReport
//
// Full comparison across all solvers and all instances.
// ============================================================

struct SolverComparisonReport {

    std::vector<std::string> solverNames;

    // solverName -> AggregateMetrics across all instances
    std::map<std::string, AggregateMetrics> metricsBySolver;

    // one row per instance, showing every solver's result on it
    std::vector<InstanceComparisonRow> instanceRows;

    // solverName -> number of instances where it was fastest
    std::map<std::string, int> winCountBySolver;

    // solverName -> average speedup relative to the baseline
    // (baseline speedup is always 1.0). Only computed over
    // instances where BOTH the baseline and the solver succeeded.
    std::map<std::string, double> averageSpeedupVsBaseline;

    std::string baselineSolverName;
};

// ============================================================
// runSolverComparison
//
// Runs every solver in `solvers` against every instance in
// `instances`, using the same timeout/memory limit for all runs,
// and builds a full SolverComparisonReport.
//
// `baselineSolverName` is used to compute relative speedups.
// If empty or not found among `solvers`, the first solver in
// the list is used as the baseline.
// ============================================================

inline SolverComparisonReport runSolverComparison(
    ProcessManager& manager,
    const std::vector<SolverSpec>& solvers,
    const std::vector<BenchmarkInstance>& instances,
    std::chrono::milliseconds timeout,
    long long memoryLimitKB = -1,
    std::string baselineSolverName = ""
) {
    SolverComparisonReport report;

    if (solvers.empty()) {
        return report;
    }

    for (const auto& solver : solvers) {
        report.solverNames.push_back(solver.name);
    }

    if (
        baselineSolverName.empty() ||
        std::find(
            report.solverNames.begin(),
            report.solverNames.end(),
            baselineSolverName
        ) == report.solverNames.end()
    ) {
        baselineSolverName = solvers.front().name;
    }

    report.baselineSolverName = baselineSolverName;

    // ------------------------------------------------------
    // Run every solver against every instance
    // ------------------------------------------------------

    // solverName -> all its BenchmarkResults (for AggregateMetrics)
    std::map<std::string, std::vector<BenchmarkResult>> allResultsBySolver;

    for (const auto& solver : solvers) {
        report.winCountBySolver[solver.name] = 0;
    }

    for (const auto& instance : instances) {

        InstanceComparisonRow row;
        row.instanceName = instance.name;

        std::optional<std::string> fastestSolverName;
        std::chrono::milliseconds fastestRuntime{0};
        bool fastestFound = false;

        for (const auto& solver : solvers) {

            BenchmarkResult result = runBenchmark(
                manager,
                solver.name,
                instance.name,
                solver.executablePath,
                { instance.path },
                timeout,
                memoryLimitKB
            );

            row.resultsBySolver[solver.name] = result;
            allResultsBySolver[solver.name].push_back(result);

            if (result.isSuccess()) {

                if (!fastestFound || result.runtime < fastestRuntime) {
                    fastestFound = true;
                    fastestRuntime = result.runtime;
                    fastestSolverName = solver.name;
                }
            }
        }

        row.fastestSolver = fastestSolverName;

        if (fastestSolverName.has_value()) {
            report.winCountBySolver[fastestSolverName.value()]++;
        }

        report.instanceRows.push_back(row);
    }

    // ------------------------------------------------------
    // Per-solver aggregate metrics
    // ------------------------------------------------------

    for (const auto& solver : solvers) {
        report.metricsBySolver[solver.name] =
            computeAggregateMetrics(allResultsBySolver[solver.name]);
    }

    // ------------------------------------------------------
    // Average speedup vs baseline
    //
    // speedup = baseline_runtime / solver_runtime
    // (>1.0 means the solver is faster than baseline,
    //  <1.0 means slower)
    //
    // Only counted on instances where BOTH the baseline and
    // the solver succeeded.
    // ------------------------------------------------------

    for (const auto& solver : solvers) {

        double speedupSum = 0.0;
        int comparableCount = 0;

        for (const auto& row : report.instanceRows) {

            auto baselineIt = row.resultsBySolver.find(baselineSolverName);
            auto solverIt = row.resultsBySolver.find(solver.name);

            if (
                baselineIt == row.resultsBySolver.end() ||
                solverIt == row.resultsBySolver.end()
            ) {
                continue;
            }

            const BenchmarkResult& baselineResult = baselineIt->second;
            const BenchmarkResult& solverResult = solverIt->second;

            if (!baselineResult.isSuccess() || !solverResult.isSuccess()) {
                continue;
            }

            if (solverResult.runtime.count() == 0) {
                continue;   // avoid divide-by-zero
            }

            double speedup =
                static_cast<double>(baselineResult.runtime.count()) /
                static_cast<double>(solverResult.runtime.count());

            speedupSum += speedup;
            comparableCount++;
        }

        report.averageSpeedupVsBaseline[solver.name] =
            (comparableCount > 0) ? (speedupSum / comparableCount) : 0.0;
    }

    return report;
}

// ============================================================
// summarize (SolverComparisonReport overload)
//
// Multi-line, human-readable comparison table for console output.
// ============================================================

inline std::string summarize(const SolverComparisonReport& report) {

    std::ostringstream out;

    out << "=== Solver Comparison (baseline: "
        << report.baselineSolverName << ") ===\n";

    for (const auto& solverName : report.solverNames) {

        const AggregateMetrics& metrics =
            report.metricsBySolver.at(solverName);

        double speedup =
            report.averageSpeedupVsBaseline.count(solverName)
                ? report.averageSpeedupVsBaseline.at(solverName)
                : 0.0;

        int wins =
            report.winCountBySolver.count(solverName)
                ? report.winCountBySolver.at(solverName)
                : 0;

        out << "  " << solverName
            << " | " << summarize(metrics)
            << " | avg_speedup_vs_baseline=" << speedup
            << " | wins=" << wins
            << "\n";
    }

    out << "\n--- Per-instance breakdown ---\n";

    for (const auto& row : report.instanceRows) {

        out << row.instanceName << ": ";

        for (const auto& solverName : report.solverNames) {

            auto it = row.resultsBySolver.find(solverName);

            if (it == row.resultsBySolver.end()) {
                continue;
            }

            const BenchmarkResult& r = it->second;

            out << solverName << "="
                << toString(r.status) << "("
                << r.runtime.count() << "ms) ";
        }

        if (row.fastestSolver.has_value()) {
            out << "  [fastest: " << row.fastestSolver.value() << "]";
        } else {
            out << "  [no successful solver]";
        }

        out << "\n";
    }

    return out.str();
}

} // namespace benchmark