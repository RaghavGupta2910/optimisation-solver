#include "postsolver/postsolver.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace postsolver {

ValidationMetrics Postsolver::validate(const model::Model& originalModel, const Solution& sol) {
    ValidationMetrics v;
    v.isValid = true;

    if (sol.primalValues.size() != originalModel.variables.size()) {
        v.isValid = false;
        return v;
    }

    // 1. Bounds Validation
    for (size_t i = 0; i < originalModel.variables.size(); ++i) {
        double val = sol.primalValues[i];
        const auto& var = originalModel.variables[i];

        if (val < var.lowerBound - tolerance_) {
            double viol = var.lowerBound - val;
            v.maxBoundViolation = std::max(v.maxBoundViolation, viol);
        }
        if (val > var.upperBound + tolerance_) {
            double viol = val - var.upperBound;
            v.maxBoundViolation = std::max(v.maxBoundViolation, viol);
        }
    }

    // 2. Primal Feasibility & Numerical Residuals (Ax <= b, Ax == b, etc.)
    for (const auto& constr : originalModel.constraints) {
        double lhs = 0.0;
        for (const auto& term : constr.terms) {
            lhs += term.coefficient * sol.primalValues[term.varIndex];
        }

        if (lhs < constr.lowerBound - tolerance_) {
            v.maxPrimalInfeasibility = std::max(v.maxPrimalInfeasibility, constr.lowerBound - lhs);
        }
        if (lhs > constr.upperBound + tolerance_) {
            v.maxPrimalInfeasibility = std::max(v.maxPrimalInfeasibility, lhs - constr.upperBound);
        }
    }

    // 3. Objective Calculation Check
    double computedObj = originalModel.objective.constant;
    for (const auto& term : originalModel.objective.linearTerms) {
        computedObj += term.value * sol.primalValues[term.varIndex];
    }
    v.absoluteObjectiveResidual = std::abs(computedObj - sol.objectiveValue);

    // Overall Validity Gate
    if (v.maxBoundViolation > tolerance_ || v.maxPrimalInfeasibility > tolerance_) {
        v.isValid = false;
    }

    return v;
}

Solution Postsolver::unpresolve(const model::Model& originalModel, const Solution& reducedSolution) {
    Solution fullSol = reducedSolution;

    // Validate reconstructed solution
    ValidationMetrics metrics = validate(originalModel, fullSol);
    if (!metrics.isValid && fullSol.status == SolverResult::OPTIMAL) {
        fullSol.status = SolverResult::NUMERICAL_ERROR;
    }

    return fullSol;
}

} // namespace postsolver