// Ablation harness.
//
// Runs the engine over a family of instances under several configurations and
// reports the work each needed. The comparable unit of work is a step trial,
// not an iteration: every trial costs one pass over the matrix whether or not
// the linesearch accepts it, so a policy that halves iterations while doubling
// trials has bought nothing.
//
//   ./pdlp_ablation <instance.lp.txt> [more...]

#include "pdlp/pdlp_solver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

double readToken(std::FILE* file) {
    char buffer[64];
    if (std::fscanf(file, "%63s", buffer) != 1) {
        return 0.0;
    }
    if (std::strcmp(buffer, "inf") == 0) {
        return HUGE_VAL;
    }
    if (std::strcmp(buffer, "-inf") == 0) {
        return -HUGE_VAL;
    }
    return std::atof(buffer);
}

pdlp::CompiledLp load(const char* path) {
    std::FILE* file = std::fopen(path, "r");
    if (file == nullptr) {
        std::fprintf(stderr, "cannot open %s\n", path);
        std::exit(1);
    }
    int rows = 0;
    int columns = 0;
    long nonzeros = 0;
    if (std::fscanf(file, "%d %d %ld", &rows, &columns, &nonzeros) != 3) {
        std::exit(1);
    }

    pdlp::CompiledLp problem;
    problem.objectiveOffset = readToken(file);
    problem.objective.resize(static_cast<std::size_t>(columns));
    for (double& value : problem.objective) { value = readToken(file); }
    problem.variableLower.resize(static_cast<std::size_t>(columns));
    for (double& value : problem.variableLower) { value = readToken(file); }
    problem.variableUpper.resize(static_cast<std::size_t>(columns));
    for (double& value : problem.variableUpper) { value = readToken(file); }
    problem.rowLower.resize(static_cast<std::size_t>(rows));
    for (double& value : problem.rowLower) { value = readToken(file); }
    problem.rowUpper.resize(static_cast<std::size_t>(rows));
    for (double& value : problem.rowUpper) { value = readToken(file); }

    std::vector<pdlp::MatrixTriplet> triplets(static_cast<std::size_t>(nonzeros));
    for (long k = 0; k < nonzeros; ++k) {
        const int row = static_cast<int>(readToken(file));
        const int column = static_cast<int>(readToken(file));
        triplets[static_cast<std::size_t>(k)] = {row, column, readToken(file)};
    }
    std::fclose(file);
    problem.matrix = pdlp::SparseMatrix::fromTriplets(rows, columns, std::move(triplets));
    return problem;
}

struct Configuration {
    const char* name;
    bool linesearch;
    bool ratchet;
    bool halpern = false;
    bool averaging = true;
    bool restarts = true;
    int minimumRestart = 64;
    bool anderson = false;
    int andersonDepth = 5;
};

}  // namespace

int main(int argc, char** argv) {
    const Configuration configurations[] = {
        {"linesearch (current best)", true,  false, false, true, true, 64, false, 5},
        {"fixed step (T constant)",   false, false, false, true, true, 64, false, 5},
        {"fixed step + Anderson 2",   false, false, false, true, true, 64, true,  2},
        {"fixed step + Anderson 5",   false, false, false, true, true, 64, true,  5},
    };

    std::vector<pdlp::CompiledLp> problems;
    for (int i = 1; i < argc; ++i) {
        problems.push_back(load(argv[i]));
    }

    std::printf("%-26s %12s %12s %10s %9s %10s %9s\n",
                "configuration", "trials->1e-8", "solved", "trials/it",
                "restarts", "eta/bound", "seconds");
    std::printf("%s\n", std::string(94, '-').c_str());

    for (const Configuration& configuration : configurations) {
        double logTrials = 0.0;
        double seconds = 0.0;
        double trialRatio = 0.0;
        double stepRatio = 0.0;
        long restarts = 0;
        int solved = 0;
        int counted = 0;

        for (const pdlp::CompiledLp& problem : problems) {
            pdlp::PdlpOptions options;
            options.iterationLimit = 40000;
            options.primalTolerance = 1e-8;
            options.dualTolerance = 1e-8;
            options.gapTolerance = 1e-8;
            options.terminationCheckFrequency = 40;
            options.useFeasibilityPolishing = false;
            options.useAdaptiveLinesearch = configuration.linesearch;
            options.useAdaptiveSteps = configuration.ratchet;
            options.useHalpern = configuration.halpern;
            options.useAveraging = configuration.averaging;
            options.useRestarts = configuration.restarts;
            options.minimumRestartIterations = configuration.minimumRestart;
            options.useAnderson = configuration.anderson;
            options.andersonDepth = configuration.andersonDepth;

            const pdlp::PdlpResult result = pdlp::PdlpSolver{}.solve(problem, options);

            // Work to reach tolerance, in matrix passes. An unconverged solve is
            // charged the full budget, so a policy cannot win by giving up.
            const double work = (result.status == pdlp::PdlpStatus::Optimal)
                ? static_cast<double>(result.stepTrials)
                : static_cast<double>(options.iterationLimit);
            logTrials += std::log10(std::max(work, 1.0));
            if (result.status == pdlp::PdlpStatus::Optimal) {
                ++solved;
            }
            seconds += result.solveTimeSeconds;
            restarts += result.restartCount;
            if (result.iterations > 0) {
                trialRatio += static_cast<double>(result.stepTrials) /
                    static_cast<double>(result.iterations);
            }
            if (result.staticStepBound > 0.0) {
                stepRatio += result.finalStepSize / result.staticStepBound;
            }
            ++counted;
        }

        if (counted == 0) {
            std::printf("%-26s  no results\n", configuration.name);
            continue;
        }
        char solvedText[32];
        std::snprintf(solvedText, sizeof(solvedText), "%d/%d", solved, counted);
        std::printf("%-26s %12.0f %12s %10.2f %9ld %10.2f %9.2f\n",
                    configuration.name,
                    std::pow(10.0, logTrials / counted),
                    solvedText,
                    trialRatio / counted,
                    restarts,
                    stepRatio / counted,
                    seconds);
    }
    return 0;
}
