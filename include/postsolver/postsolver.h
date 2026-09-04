#pragma once

#include "model/model.h"
#include "presolve/presolve_result.h"

#include <cstddef>
#include <vector>

namespace postsolve {

constexpr double DEFAULT_POSTSOLVE_TOLERANCE = 1e-6;

enum class PostsolveStatus {
  SUCCESS,
  PRESOLVE_INFEASIBLE,
  DIMENSION_MISMATCH,
  BOUND_VIOLATION,
  CONSTRAINT_VIOLATION,
  INVALID_SOLUTION
};

struct PostsolveResult {
  PostsolveStatus status = PostsolveStatus::INVALID_SOLUTION;
  std::vector<double> primalValues; // Restored to original variable ordering
  double objectiveValue = 0.0;
  double maxBoundViolation = 0.0;
  double maxConstraintViolation = 0.0;
};

class Postsolver {
public:
  explicit Postsolver(double tolerance = DEFAULT_POSTSOLVE_TOLERANCE)
      : tolerance_(tolerance) {}

  PostsolveResult process(
      const model::Model& originalModel,
      const presolve::PresolveResult& presolveResult,
      const std::vector<double>& presolvedPrimalSolution
  );

  double evaluateObjective(
      const model::Model& originalModel,
      const std::vector<double>& fullPrimalSolution
  ) const;

private:
  double tolerance_;

  bool validateSolution(
      const model::Model& originalModel,
      const std::vector<double>& fullPrimalSolution,
      PostsolveResult& result
  ) const;
};

} // namespace postsolve