#ifndef POSTSOLVER_H
#define POSTSOLVER_H

#include <vector>
#include <string>
#include <cmath>
#include <limits>
#include "model/model.h"

namespace postsolver {

// Statuses matching your architecture diagram
enum class SolverResult {
    OPTIMAL,
    FEASIBLE,
    INFEASIBLE,
    UNBOUNDED,
    TIME_LIMIT,
    NUMERICAL_ERROR,
    ERROR
};

struct Solution {
    std::vector<double> primalValues;  // x
    std::vector<double> dualValues;    // lambda / pi
    double objectiveValue = 0.0;
    SolverResult status = SolverResult::ERROR;
};

struct ValidationMetrics {
    double maxPrimalInfeasibility = 0.0;
    double maxDualInfeasibility = 0.0;
    double maxBoundViolation = 0.0;
    double maxIntegralityViolation = 0.0;
    double absoluteObjectiveResidual = 0.0;
    bool isValid = false;
};

class Postsolver {
public:
    Postsolver(double tol = 1e-6) : tolerance_(tol) {}

    // Un-presolve mapping
    Solution unpresolve(const model::Model& originalModel, const Solution& reducedSolution);

    // Validator layer matching your diagram
    ValidationMetrics validate(const model::Model& originalModel, const Solution& sol);

private:
    double tolerance_;
};

} // namespace postsolver

#endif // POSTSOLVER_H