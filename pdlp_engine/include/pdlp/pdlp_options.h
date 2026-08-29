#pragma once

#include <cstdint>

namespace pdlp {

struct PdlpOptions {
    std::int64_t iterationLimit = 200000;
    double timeLimitSeconds = 0.0;

    double primalTolerance = 1e-6;
    double dualTolerance = 1e-6;
    double gapTolerance = 1e-6;

    int terminationCheckFrequency = 100;
    int powerIterationCount = 100;

    // Worker threads. 0 selects hardware_concurrency; 1 forces serial
    // execution. Small models run serially regardless: below
    // parallelNonzeroThreshold the barrier costs more than the work.
    int threadCount = 0;
    std::int64_t parallelNonzeroThreshold = 20000;

    double initialStepSafety = 0.90;
    double minimumStepScale = 1e-6;
    double maximumPrimalWeight = 1e6;
    double minimumPrimalWeight = 1e-6;

    // Ruiz equilibration of A before solving. The solver iterates on the
    // equilibrated problem but always tests termination on the original, so
    // tolerances stay in the caller's units.
    bool useRuizScaling = true;
    int ruizIterations = 10;

    bool useDiagonalPreconditioning = true;

    // PDLP adaptive linesearch. When enabled the step size is free to exceed the
    // static Pock-Chambolle bound, which is the single largest reduction in
    // iteration count available; the static bound is used only to initialise it.
    // useAdaptiveSteps selects the older bounded ratchet and applies only when
    // the linesearch is off.
    // Halpern acceleration: z^{k+1} = lambda_k T(z^k) + (1-lambda_k) z^anchor,
    // with lambda_k = (k+1)/(k+2) counted from the last restart. The anchor pull
    // decays as the cycle proceeds, and each restart re-anchors.
    bool useHalpern = false;

    // Safeguarded Anderson acceleration on the PDHG fixed-point map. The
    // safeguard costs one extra matrix pass per iteration, and the method
    // assumes a fixed operator T, which an adaptive step size violates.
    bool useAnderson = false;
    int andersonDepth = 5;

    bool useAdaptiveLinesearch = true;
    int maximumStepTrials = 60;
    bool useAdaptiveSteps = true;
    bool useAveraging = true;
    bool useRestarts = true;
    bool useFeasibilityPolishing = true;

    // Restart policy. See RestartController for what each threshold triggers.
    int minimumRestartIterations = 64;
    int maximumRestartIterations = 100000;
    double restartSufficientDecay = 0.20;
    double restartNecessaryDecay = 0.80;
    double restartArtificialFraction = 0.36;

    // Primal weight omega: primal step = eta/omega, dual step = eta*omega.
    //
    // Starts at 1 and is corrected at each restart toward the ratio of dual to
    // primal distance travelled since the previous restart, smoothed in log
    // space by primalWeightSmoothing.
    //
    // Initialising from ||c||/||b|| (as some formulations do) is deliberately
    // not the default: a single very slack one-sided row inflates ||b|| and
    // drives omega orders of magnitude off, which starves the dual step. That
    // measured worse here and could stall trivial models outright.
    double initialPrimalWeight = 1.0;
    double primalWeightSmoothing = 0.50;

    int polishingIterations = 5000;
};

}  // namespace pdlp
