#include "postsolve/postsolver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace postsolve {

PostsolveResult Postsolver::process(
    const model::Model& originalModel,
    const presolve::PresolveResult& presolveResult,
    const std::vector<double>& presolvedPrimalSolution
) {
  PostsolveResult result;

  // 1. Handle presolve infeasibility immediately
  if (presolveResult.infeasible) {
    result.status = PostsolveStatus::PRESOLVE_INFEASIBLE;
    return result;
  }

  std::size_t origVarCount = originalModel.variables.size();
  std::vector<double> currentSolution(origVarCount, 0.0);

  // If the presolved model variables match the solution vector size, populate initial values
  if (presolveResult.model.variables.size() != presolvedPrimalSolution.size()) {
    result.status = PostsolveStatus::DIMENSION_MISMATCH;
    return result;
  }

  // Copy available presolved variables
  for (std::size_t i = 0; i < presolvedPrimalSolution.size(); ++i) {
    currentSolution[i] = presolvedPrimalSolution[i];
  }

  // 2. Process transformations in REVERSE ORDER (T_N -> T_1)
  const auto& history = presolveResult.transformations;
  for (auto it = history.rbegin(); it != history.rend(); ++it) {
    const auto& t = *it;

    switch (t.type) {
      case presolve::TransformationType::FixVariable:
      case presolve::TransformationType::SingletonRowFixing: {
        currentSolution[t.targetVarIndex] = t.constant;
        break;
      }

      case presolve::TransformationType::EmptyColumn: {
        // Restore value safely within original variable bounds
        const auto& var = originalModel.variables[t.targetVarIndex];
        double restoredVal = 0.0;
        if (restoredVal < var.lowerBound) restoredVal = var.lowerBound;
        if (restoredVal > var.upperBound) restoredVal = var.upperBound;

        currentSolution[t.targetVarIndex] = restoredVal;
        break;
      }

      case presolve::TransformationType::AggregateVariable: {
        // x = constant + sum(coeff_i * y_i)
        double val = t.constant;
        for (const auto& term : t.substitutionTerms) {
          val += term.coefficient * currentSolution[term.varIndex];
        }
        currentSolution[t.targetVarIndex] = val;
        break;
      }

      case presolve::TransformationType::BoundTightening:
      case presolve::TransformationType::RemoveRow:
        break;
    }
  }

  result.primalValues = currentSolution;

  // 3. Evaluate objective on full original solution
  result.objectiveValue = evaluateObjective(originalModel, result.primalValues);

  // 4. Validate bounds and constraints
  if (!validateSolution(originalModel, result.primalValues, result)) {
    return result;
  }

  result.status = PostsolveStatus::SUCCESS;
  return result;
}

double Postsolver::evaluateObjective(
    const model::Model& originalModel,
    const std::vector<double>& fullPrimalSolution
) const {
  double obj = originalModel.objective.constant;

  for (const auto& term : originalModel.objective.linearTerms) {
    obj += term.value * fullPrimalSolution[term.varIndex];
  }

  for (const auto& qterm : originalModel.objective.quadraticTerms) {
    obj += 0.5 * qterm.value * fullPrimalSolution[qterm.row] * fullPrimalSolution[qterm.col];
  }

  return obj;
}

bool Postsolver::validateSolution(
    const model::Model& originalModel,
    const std::vector<double>& sol,
    PostsolveResult& result
) const {
  result.maxBoundViolation = 0.0;
  result.maxConstraintViolation = 0.0;

  // Check bounds
  for (std::size_t i = 0; i < originalModel.variables.size(); ++i) {
    const auto& var = originalModel.variables[i];
    double val = sol[i];

    if (val < var.lowerBound - tolerance_) {
      result.maxBoundViolation = std::max(result.maxBoundViolation, var.lowerBound - val);
    }
    if (val > var.upperBound + tolerance_) {
      result.maxBoundViolation = std::max(result.maxBoundViolation, val - var.upperBound);
    }
  }

  if (result.maxBoundViolation > tolerance_) {
    result.status = PostsolveStatus::BOUND_VIOLATION;
    return false;
  }

  // Check constraints
  for (const auto& constr : originalModel.constraints) {
    double activity = 0.0;
    for (const auto& term : constr.terms) {
      activity += term.coefficient * sol[term.varIndex];
    }

    if (activity < constr.lowerBound - tolerance_) {
      result.maxConstraintViolation = std::max(result.maxConstraintViolation, constr.lowerBound - activity);
    }
    if (activity > constr.upperBound + tolerance_) {
      result.maxConstraintViolation = std::max(result.maxConstraintViolation, activity - constr.upperBound);
    }
  }

  if (result.maxConstraintViolation > tolerance_) {
    result.status = PostsolveStatus::CONSTRAINT_VIOLATION;
    return false;
  }

  return true;
}

} // namespace postsolve