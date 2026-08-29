#include "pdlp/step_controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pdlp {
namespace {

// Numerical sanity bound only. The linesearch is self-limiting -- eta_bar is a
// property of the local curvature, not of the current step -- so no algorithmic
// ceiling is imposed on top of it.
constexpr double kAbsoluteMaximumStep = 1e12;

}  // namespace

StepController::StepController(
    const PdlpOptions& options,
    double maximumSafeGlobalStep
) : options_(options) {
    parameters_.maximumSafeGlobalStep = maximumSafeGlobalStep;
    parameters_.globalStep = std::clamp(
        options.initialStepSafety * maximumSafeGlobalStep,
        options.minimumStepScale * maximumSafeGlobalStep,
        maximumSafeGlobalStep
    );
    parameters_.primalWeight = 1.0;
}

bool StepController::evaluateTrial(
    double primalMovementWeighted,
    double dualMovementWeighted,
    double interaction,
    std::int64_t iteration
) noexcept {
    const double omega = parameters_.primalWeight;
    const double movement =
        omega * primalMovementWeighted + dualMovementWeighted / omega;
    const double coupling = std::abs(interaction);

    // No coupling means the step is unconstrained by curvature in this
    // direction; the growth cap alone then governs.
    double limit = std::numeric_limits<double>::infinity();
    if (coupling > 0.0 && movement > 0.0) {
        limit = movement / (2.0 * coupling);
    }

    // Offset by two so the shrink factor is never zero on the first iteration,
    // which would collapse the step to nothing.
    const double index = static_cast<double>(iteration) + 2.0;
    const double shrink = 1.0 - std::pow(index, -0.3);
    const double grow = 1.0 + std::pow(index, -0.6);

    const bool accepted = parameters_.globalStep <= limit;

    double next = std::min(shrink * limit, grow * parameters_.globalStep);
    next = std::clamp(
        next,
        options_.minimumStepScale * parameters_.maximumSafeGlobalStep,
        kAbsoluteMaximumStep
    );
    if (!std::isfinite(next) || next <= 0.0) {
        next = parameters_.globalStep;
    }
    parameters_.globalStep = next;

    return accepted;
}

void StepController::observeProgress(double kktScore) {
    if (options_.useAdaptiveLinesearch || !options_.useAdaptiveSteps ||
        !std::isfinite(kktScore)) {
        previousScore_ = kktScore;
        return;
    }

    // The score observed here must be the current iterate's, not the best seen
    // so far: a best-so-far score is monotonically non-increasing, which makes
    // the backoff branch unreachable and turns the controller into a one-way
    // ratchet up to the safety cap.
    if (std::isfinite(previousScore_)) {
        if (kktScore <= 0.70 * previousScore_) {
            parameters_.globalStep *= 1.02;
        } else if (kktScore > 1.05 * previousScore_) {
            parameters_.globalStep *= 0.70;
        }
    }

    parameters_.globalStep = std::clamp(
        parameters_.globalStep,
        options_.minimumStepScale * parameters_.maximumSafeGlobalStep,
        parameters_.maximumSafeGlobalStep
    );
    previousScore_ = kktScore;
}

void StepController::setPrimalWeight(double weight) noexcept {
    if (weight > 0.0 && std::isfinite(weight)) {
        parameters_.primalWeight = std::clamp(
            weight,
            options_.minimumPrimalWeight,
            options_.maximumPrimalWeight
        );
    }
}

void StepController::updatePrimalWeight(
    double primalDistance,
    double dualDistance
) {
    if (primalDistance <= 1e-16 || dualDistance <= 1e-16 ||
        !std::isfinite(primalDistance) || !std::isfinite(dualDistance)) {
        return;
    }

    // omega balances the primal and dual contributions to the weighted distance
    // omega*||dx||^2 + ||dy||^2/omega, which is minimised at omega = |dy|/|dx|.
    const double target = std::clamp(dualDistance / primalDistance, 1e-12, 1e12);
    const double smoothing = std::clamp(options_.primalWeightSmoothing, 0.0, 1.0);
    const double updated = std::exp(
        smoothing * std::log(target) +
        (1.0 - smoothing) * std::log(parameters_.primalWeight)
    );
    setPrimalWeight(updated);
}

}  // namespace pdlp
