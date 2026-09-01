#pragma once

#include "model/model.h"
#include "presolve/presolve_result.h"

#include <cstddef>

namespace presolve {

class Presolver {
public:
  PresolveResult run(const model::Model &input);

private:
  // ==========================================================
  // Batch 1
  // ==========================================================

  void removeVariable(model::Model &model, std::size_t variableIndex);

  void removeZeroCoefficients(model::Model &model);

  bool fixVariable(model::Model &model, std::size_t variableIndex,
                   double value);

  // ==========================================================
  // Batch 2
  // ==========================================================

  // 5. Singleton rows
  bool processSingletonRow(model::Model &model, std::size_t constraintIndex,
                           PresolveResult &result);

  // 6. Simple infeasibility detection
  bool detectSimpleInfeasibility(const model::Model &model) const;

  // 7. Bound tightening
  bool tightenBounds(model::Model &model, PresolveResult &result);

  // 8. Implied bounds
  bool applyImpliedBounds(model::Model &model, PresolveResult &result);

  // 9. Redundant constraints
  bool removeRedundantConstraints(model::Model &model, PresolveResult &result);

  // 10/13. Duplicate / parallel rows
  bool removeDuplicateAndParallelRows(model::Model &model,
                                      PresolveResult &result);

  // 11. Dominated columns
  bool removeDominatedColumns(model::Model &model, PresolveResult &result);

  // 12. Parallel columns
  bool processParallelColumns(model::Model &model, PresolveResult &result);

  // 14/15. Dependent equations / rank dependency
  bool detectDependentEquations(model::Model &model, PresolveResult &result);

  // ==========================================================
  // Helpers
  // ==========================================================

  double coefficientForVariable(const model::Constraint &constraint,
                                std::size_t variableIndex) const;

  double minConstraintActivity(const model::Constraint &constraint,
                               const model::Model &model) const;

  double maxConstraintActivity(const model::Constraint &constraint,
                               const model::Model &model) const;

  bool constraintsEquivalent(const model::Constraint &a,
                             const model::Constraint &b) const;

  bool constraintsParallel(const model::Constraint &a,
                           const model::Constraint &b) const;

  bool equationsDependent(const model::Constraint &a,
                          const model::Constraint &b) const;
};

} // namespace presolve
