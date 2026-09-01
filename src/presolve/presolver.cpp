#include "presolve/presolver.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace presolve {

namespace {

constexpr double EPS = 1e-9;

bool approximatelyEqual(double a, double b) {
  if (std::isinf(a) || std::isinf(b)) {
    return a == b;
  }

  return std::abs(a - b) <= EPS;
}

bool isFinite(double value) { return std::isfinite(value); }

} // namespace

// ============================================================
// VARIABLE REMOVAL
// ============================================================

void Presolver::removeVariable(model::Model &model, std::size_t variableIndex) {

  if (variableIndex >= model.variables.size()) {
    return;
  }

  model.variables.erase(model.variables.begin() + variableIndex);

  for (auto &term : model.objective.linearTerms) {

    if (term.variableIndex > static_cast<int>(variableIndex)) {

      --term.variableIndex;
    }
  }

  model.objective.linearTerms.erase(
      std::remove_if(model.objective.linearTerms.begin(),
                     model.objective.linearTerms.end(),
                     [variableIndex](const model::LinearTerm &term) {
                       return term.variableIndex ==
                              static_cast<int>(variableIndex);
                     }),
      model.objective.linearTerms.end());

  for (auto &constraint : model.constraints) {

    for (auto &term : constraint.linearTerms) {

      if (term.variableIndex > static_cast<int>(variableIndex)) {

        --term.variableIndex;
      }
    }

    constraint.linearTerms.erase(
        std::remove_if(
            constraint.linearTerms.begin(), constraint.linearTerms.end(),
            [variableIndex](const model::LinearTerm &term) {
              return term.variableIndex == static_cast<int>(variableIndex);
            }),
        constraint.linearTerms.end());
  }
}

// ============================================================
// ZERO COEFFICIENTS
// ============================================================

void Presolver::removeZeroCoefficients(model::Model &model) {

  model.objective.linearTerms.erase(
      std::remove_if(model.objective.linearTerms.begin(),
                     model.objective.linearTerms.end(),
                     [](const model::LinearTerm &term) {
                       return std::abs(term.value) <= EPS;
                     }),
      model.objective.linearTerms.end());

  for (auto &constraint : model.constraints) {

    constraint.linearTerms.erase(
        std::remove_if(constraint.linearTerms.begin(),
                       constraint.linearTerms.end(),
                       [](const model::LinearTerm &term) {
                         return std::abs(term.value) <= EPS;
                       }),
        constraint.linearTerms.end());
  }
}

// ============================================================
// FIX VARIABLE
// ============================================================

bool Presolver::fixVariable(model::Model &model, std::size_t variableIndex,
                            double value) {

  if (variableIndex >= model.variables.size()) {
    return false;
  }

  // Objective contribution.
  for (auto it = model.objective.linearTerms.begin();
       it != model.objective.linearTerms.end();) {

    if (it->variableIndex == static_cast<int>(variableIndex)) {

      model.objective.offset += it->value * value;

      it = model.objective.linearTerms.erase(it);

    } else {

      ++it;
    }
  }

  // Constraint contribution.
  for (auto &constraint : model.constraints) {

    for (auto it = constraint.linearTerms.begin();
         it != constraint.linearTerms.end();) {

      if (it->variableIndex == static_cast<int>(variableIndex)) {

        const double contribution = it->value * value;

        if (isFinite(constraint.lowerBound)) {
          constraint.lowerBound -= contribution;
        }

        if (isFinite(constraint.upperBound)) {
          constraint.upperBound -= contribution;
        }

        it = constraint.linearTerms.erase(it);

      } else {

        ++it;
      }
    }
  }

  removeVariable(model, variableIndex);

  return true;
}

// ============================================================
// COEFFICIENT LOOKUP
// ============================================================

double Presolver::coefficientForVariable(const model::Constraint &constraint,
                                         std::size_t variableIndex) const {

  for (const auto &term : constraint.linearTerms) {

    if (term.variableIndex == static_cast<int>(variableIndex)) {

      return term.value;
    }
  }

  return 0.0;
}

// ============================================================
// MINIMUM ACTIVITY
// ============================================================

double Presolver::minConstraintActivity(const model::Constraint &constraint,
                                        const model::Model &model) const {

  double result = 0.0;

  for (const auto &term : constraint.linearTerms) {

    if (term.variableIndex < 0 ||
        static_cast<std::size_t>(term.variableIndex) >=
            model.variables.size()) {

      continue;
    }

    const auto &variable =
        model.variables[static_cast<std::size_t>(term.variableIndex)];

    if (term.value >= 0.0) {

      if (!isFinite(variable.lowerBound)) {
        return -std::numeric_limits<double>::infinity();
      }

      result += term.value * variable.lowerBound;

    } else {

      if (!isFinite(variable.upperBound)) {
        return -std::numeric_limits<double>::infinity();
      }

      result += term.value * variable.upperBound;
    }
  }

  return result;
}

// ============================================================
// MAXIMUM ACTIVITY
// ============================================================

double Presolver::maxConstraintActivity(const model::Constraint &constraint,
                                        const model::Model &model) const {

  double result = 0.0;

  for (const auto &term : constraint.linearTerms) {

    if (term.variableIndex < 0 ||
        static_cast<std::size_t>(term.variableIndex) >=
            model.variables.size()) {

      continue;
    }

    const auto &variable =
        model.variables[static_cast<std::size_t>(term.variableIndex)];

    if (term.value >= 0.0) {

      if (!isFinite(variable.upperBound)) {
        return std::numeric_limits<double>::infinity();
      }

      result += term.value * variable.upperBound;

    } else {

      if (!isFinite(variable.lowerBound)) {
        return std::numeric_limits<double>::infinity();
      }

      result += term.value * variable.lowerBound;
    }
  }

  return result;
}

// ============================================================
// SINGLETON ROWS
// ============================================================

bool Presolver::processSingletonRow(model::Model &model,
                                    std::size_t constraintIndex,
                                    PresolveResult &result) {

  if (constraintIndex >= model.constraints.size()) {
    return false;
  }

  const model::Constraint constraint = model.constraints[constraintIndex];

  if (constraint.linearTerms.size() != 1) {
    return false;
  }

  const model::LinearTerm term = constraint.linearTerms.front();

  if (term.variableIndex < 0 ||
      static_cast<std::size_t>(term.variableIndex) >= model.variables.size()) {

    return false;
  }

  const std::size_t variableIndex =
      static_cast<std::size_t>(term.variableIndex);

  const double coefficient = term.value;

  if (std::abs(coefficient) <= EPS) {
    return false;
  }

  auto &variable = model.variables[variableIndex];

  double newLower = variable.lowerBound;

  double newUpper = variable.upperBound;

  if (coefficient > 0.0) {

    if (isFinite(constraint.lowerBound)) {

      newLower = std::max(newLower, constraint.lowerBound / coefficient);
    }

    if (isFinite(constraint.upperBound)) {

      newUpper = std::min(newUpper, constraint.upperBound / coefficient);
    }

  } else {

    if (isFinite(constraint.lowerBound)) {

      newUpper = std::min(newUpper, constraint.lowerBound / coefficient);
    }

    if (isFinite(constraint.upperBound)) {

      newLower = std::max(newLower, constraint.upperBound / coefficient);
    }
  }

  if (newLower > newUpper + EPS) {

    result.infeasible = true;
    return false;
  }

  const bool lowerChanged = !approximatelyEqual(variable.lowerBound, newLower);

  const bool upperChanged = !approximatelyEqual(variable.upperBound, newUpper);

  if (!lowerChanged && !upperChanged) {
    return false;
  }

  if (lowerChanged) {

    Transformation transformation;

    transformation.type = TransformationType::TightenLowerBound;

    transformation.index = variableIndex;

    transformation.oldValue = variable.lowerBound;

    transformation.newValue = newLower;

    transformation.reason = "Singleton row tightened variable lower bound";

    result.transformations.push_back(std::move(transformation));
  }

  if (upperChanged) {

    Transformation transformation;

    transformation.type = TransformationType::TightenUpperBound;

    transformation.index = variableIndex;

    transformation.oldValue = variable.upperBound;

    transformation.newValue = newUpper;

    transformation.reason = "Singleton row tightened variable upper bound";

    result.transformations.push_back(std::move(transformation));
  }

  variable.lowerBound = newLower;

  variable.upperBound = newUpper;

  Transformation transformation;

  transformation.type = TransformationType::RemoveConstraint;

  transformation.index = constraintIndex;

  transformation.reason = "Singleton row converted to variable bounds";

  result.transformations.push_back(std::move(transformation));

  model.constraints.erase(model.constraints.begin() + constraintIndex);

  return true;
}

// ============================================================
// SIMPLE INFEASIBILITY
// ============================================================

bool Presolver::detectSimpleInfeasibility(const model::Model &model) const {

  for (const auto &variable : model.variables) {

    if (variable.lowerBound > variable.upperBound + EPS) {

      return true;
    }
  }

  for (const auto &constraint : model.constraints) {

    if (constraint.lowerBound > constraint.upperBound + EPS) {

      return true;
    }

    const double minActivity = minConstraintActivity(constraint, model);

    const double maxActivity = maxConstraintActivity(constraint, model);

    if (isFinite(constraint.lowerBound) && isFinite(maxActivity) &&
        maxActivity < constraint.lowerBound - EPS) {

      return true;
    }

    if (isFinite(constraint.upperBound) && isFinite(minActivity) &&
        minActivity > constraint.upperBound + EPS) {

      return true;
    }
  }

  return false;
}

// ============================================================
// BOUND TIGHTENING
// ============================================================

bool Presolver::tightenBounds(model::Model &model, PresolveResult &result) {

  bool changed = false;

  for (const auto &constraint : model.constraints) {

    for (const auto &target : constraint.linearTerms) {

      if (target.variableIndex < 0) {
        continue;
      }

      const std::size_t variableIndex =
          static_cast<std::size_t>(target.variableIndex);

      if (variableIndex >= model.variables.size()) {

        continue;
      }

      const double a = target.value;

      if (std::abs(a) <= EPS) {
        continue;
      }

      double otherMin = 0.0;
      double otherMax = 0.0;

      bool otherMinFinite = true;
      bool otherMaxFinite = true;

      for (const auto &term : constraint.linearTerms) {

        if (term.variableIndex == target.variableIndex) {

          continue;
        }

        if (term.variableIndex < 0 ||
            static_cast<std::size_t>(term.variableIndex) >=
                model.variables.size()) {

          otherMinFinite = false;
          otherMaxFinite = false;
          continue;
        }

        const auto &otherVariable =
            model.variables[static_cast<std::size_t>(term.variableIndex)];

        if (term.value >= 0.0) {

          if (isFinite(otherVariable.lowerBound)) {

            otherMin += term.value * otherVariable.lowerBound;

          } else {

            otherMinFinite = false;
          }

          if (isFinite(otherVariable.upperBound)) {

            otherMax += term.value * otherVariable.upperBound;

          } else {

            otherMaxFinite = false;
          }

        } else {

          if (isFinite(otherVariable.upperBound)) {

            otherMin += term.value * otherVariable.upperBound;

          } else {

            otherMinFinite = false;
          }

          if (isFinite(otherVariable.lowerBound)) {

            otherMax += term.value * otherVariable.lowerBound;

          } else {

            otherMaxFinite = false;
          }
        }
      }

      auto &variable = model.variables[variableIndex];

      // ------------------------------------------------------
      // Lower constraint bound.
      // ------------------------------------------------------

      if (isFinite(constraint.lowerBound) && otherMaxFinite) {

        const double candidate = (constraint.lowerBound - otherMax) / a;

        if (a > 0.0) {

          if (candidate > variable.lowerBound + EPS) {

            Transformation transformation;

            transformation.type = TransformationType::TightenLowerBound;

            transformation.index = variableIndex;

            transformation.oldValue = variable.lowerBound;

            transformation.newValue = candidate;

            transformation.reason = "Constraint tightened variable lower bound";

            result.transformations.push_back(std::move(transformation));

            variable.lowerBound = candidate;

            changed = true;
          }

        } else {

          if (candidate < variable.upperBound - EPS) {

            Transformation transformation;

            transformation.type = TransformationType::TightenUpperBound;

            transformation.index = variableIndex;

            transformation.oldValue = variable.upperBound;

            transformation.newValue = candidate;

            transformation.reason = "Constraint tightened variable upper bound";

            result.transformations.push_back(std::move(transformation));

            variable.upperBound = candidate;

            changed = true;
          }
        }
      }

      // ------------------------------------------------------
      // Upper constraint bound.
      // ------------------------------------------------------

      if (isFinite(constraint.upperBound) && otherMinFinite) {

        const double candidate = (constraint.upperBound - otherMin) / a;

        if (a > 0.0) {

          if (candidate < variable.upperBound - EPS) {

            Transformation transformation;

            transformation.type = TransformationType::TightenUpperBound;

            transformation.index = variableIndex;

            transformation.oldValue = variable.upperBound;

            transformation.newValue = candidate;

            transformation.reason = "Constraint tightened variable upper bound";

            result.transformations.push_back(std::move(transformation));

            variable.upperBound = candidate;

            changed = true;
          }

        } else {

          if (candidate > variable.lowerBound + EPS) {

            Transformation transformation;

            transformation.type = TransformationType::TightenLowerBound;

            transformation.index = variableIndex;

            transformation.oldValue = variable.lowerBound;

            transformation.newValue = candidate;

            transformation.reason = "Constraint tightened variable lower bound";

            result.transformations.push_back(std::move(transformation));

            variable.lowerBound = candidate;

            changed = true;
          }
        }
      }
    }
  }

  return changed;
}

// ============================================================
// IMPLIED BOUNDS
// ============================================================

bool Presolver::applyImpliedBounds(model::Model &model,
                                   PresolveResult &result) {

  return tightenBounds(model, result);
}

// ============================================================
// REDUNDANT CONSTRAINTS
// ============================================================

bool Presolver::removeRedundantConstraints(model::Model &model,
                                           PresolveResult &result) {

  bool changed = false;

  for (std::size_t i = model.constraints.size(); i-- > 0;) {

    const auto &constraint = model.constraints[i];

    const double minActivity = minConstraintActivity(constraint, model);

    const double maxActivity = maxConstraintActivity(constraint, model);

    bool redundant = true;

    if (isFinite(constraint.lowerBound)) {

      if (!isFinite(minActivity) || minActivity < constraint.lowerBound - EPS) {

        redundant = false;
      }
    }

    if (isFinite(constraint.upperBound)) {

      if (!isFinite(maxActivity) || maxActivity > constraint.upperBound + EPS) {

        redundant = false;
      }
    }

    if (!redundant) {
      continue;
    }

    Transformation transformation;

    transformation.type = TransformationType::RemoveConstraint;

    transformation.index = i;

    transformation.reason = "Constraint is redundant given variable bounds";

    result.transformations.push_back(std::move(transformation));

    model.constraints.erase(model.constraints.begin() + i);

    changed = true;
  }

  return changed;
}

// ============================================================
// ROW SIGNATURE
// ============================================================

namespace {

std::map<int, double> buildCoefficientMap(const model::Constraint &constraint) {

  std::map<int, double> coefficients;

  for (const auto &term : constraint.linearTerms) {

    if (std::abs(term.value) > EPS) {

      coefficients[term.variableIndex] += term.value;
    }
  }

  return coefficients;
}

} // namespace

// ============================================================
// DUPLICATE ROWS
// ============================================================

bool Presolver::constraintsEquivalent(const model::Constraint &a,
                                      const model::Constraint &b) const {

  const auto coefficientsA = buildCoefficientMap(a);

  const auto coefficientsB = buildCoefficientMap(b);

  if (coefficientsA.size() != coefficientsB.size()) {

    return false;
  }

  for (const auto &[index, valueA] : coefficientsA) {

    auto it = coefficientsB.find(index);

    if (it == coefficientsB.end()) {
      return false;
    }

    if (!approximatelyEqual(valueA, it->second)) {

      return false;
    }
  }

  return approximatelyEqual(a.lowerBound, b.lowerBound) &&
         approximatelyEqual(a.upperBound, b.upperBound);
}

// ============================================================
// PARALLEL ROWS
// ============================================================

bool Presolver::constraintsParallel(const model::Constraint &a,
                                    const model::Constraint &b) const {

  const auto coefficientsA = buildCoefficientMap(a);

  const auto coefficientsB = buildCoefficientMap(b);

  std::map<int, double> indices = coefficientsA;

  for (const auto &[index, value] : coefficientsB) {

    (void)value;
    indices.emplace(index, 0.0);
  }

  double scale = 0.0;
  bool foundScale = false;

  for (const auto &[index, unused] : indices) {

    (void)unused;

    const double valueA =
        coefficientsA.count(index) ? coefficientsA.at(index) : 0.0;

    const double valueB =
        coefficientsB.count(index) ? coefficientsB.at(index) : 0.0;

    if (std::abs(valueA) <= EPS && std::abs(valueB) <= EPS) {

      continue;
    }

    if (std::abs(valueA) <= EPS || std::abs(valueB) <= EPS) {

      return false;
    }

    const double currentScale = valueB / valueA;

    if (!foundScale) {

      scale = currentScale;
      foundScale = true;

    } else if (!approximatelyEqual(currentScale, scale)) {

      return false;
    }
  }

  return foundScale;
}

// ============================================================
// REMOVE DUPLICATE / EQUIVALENT PARALLEL ROWS
// ============================================================

bool Presolver::removeDuplicateAndParallelRows(model::Model &model,
                                               PresolveResult &result) {

  bool changed = false;

  for (std::size_t i = 0; i < model.constraints.size(); ++i) {

    for (std::size_t j = model.constraints.size(); j-- > i + 1;) {

      const auto &a = model.constraints[i];
      const auto &b = model.constraints[j];

      // ======================================================
      // 1. EXACT DUPLICATE
      // ======================================================
      //
      // Same coefficients and exactly the same bounds.
      //
      // Example:
      //
      //   x + y >= 5
      //   x + y >= 5
      //
      // Keep the first and remove the second.
      // ======================================================

      if (constraintsEquivalent(a, b)) {

        Transformation transformation;

        transformation.type = TransformationType::RemoveConstraint;

        transformation.index = j;

        transformation.reason = "Duplicate constraint removed";

        result.transformations.push_back(std::move(transformation));

        model.constraints.erase(model.constraints.begin() + j);

        changed = true;

        continue;
      }

      // ======================================================
      // 2. PARALLEL / SCALED CONSTRAINTS
      // ======================================================
      //
      // Two constraints are parallel when their coefficient
      // vectors differ only by a non-zero scaling factor.
      //
      // Example:
      //
      //   x + y >= 5
      //
      //   2x + 2y >= 10
      //
      // The second constraint describes exactly the same
      // feasible half-space and can therefore be removed.
      //
      // We ONLY remove the second constraint when its bounds
      // are also scaled consistently.
      //
      // This is important:
      //
      //   x + y <= 10
      //   2x + 2y <= 15
      //
      // are parallel but NOT equivalent.
      //
      // Therefore the second constraint must remain.
      // ======================================================

      if (!constraintsParallel(a, b)) {
        continue;
      }

      // ------------------------------------------------------
      // Find the scaling factor:
      //
      //     b = scale * a
      //
      // constraintsParallel() already established that a
      // single consistent non-zero scale exists.
      // ------------------------------------------------------

      const auto coefficientsA = buildCoefficientMap(a);
      const auto coefficientsB = buildCoefficientMap(b);

      double scale = 0.0;
      bool foundScale = false;

      for (const auto &[index, coefficientA] : coefficientsA) {

        if (std::abs(coefficientA) <= EPS) {
          continue;
        }

        auto it = coefficientsB.find(index);

        if (it == coefficientsB.end()) {
          continue;
        }

        scale = it->second / coefficientA;

        foundScale = true;

        break;
      }

      if (!foundScale || std::abs(scale) <= EPS) {
        continue;
      }

      // ------------------------------------------------------
      // Calculate the bounds that b SHOULD have if it is
      // exactly the scaled version of a.
      //
      // Positive scale:
      //
      //   [L, U] -> [scale*L, scale*U]
      //
      // Negative scale reverses the interval:
      //
      //   [L, U] -> [scale*U, scale*L]
      // ------------------------------------------------------

      double expectedLower;
      double expectedUpper;

      if (scale > 0.0) {

        expectedLower = scale * a.lowerBound;
        expectedUpper = scale * a.upperBound;

      } else {

        expectedLower = scale * a.upperBound;
        expectedUpper = scale * a.lowerBound;
      }

      // ------------------------------------------------------
      // The constraints are equivalent only if BOTH bounds
      // match the scaled bounds.
      // ------------------------------------------------------

      if (!approximatelyEqual(b.lowerBound, expectedLower) ||
          !approximatelyEqual(b.upperBound, expectedUpper)) {

        continue;
      }

      // ======================================================
      // b is an exactly equivalent scaled copy of a.
      // ======================================================

      Transformation transformation;

      transformation.type = TransformationType::RemoveConstraint;

      transformation.index = j;

      transformation.reason = "Parallel equivalent constraint removed";

      result.transformations.push_back(std::move(transformation));

      model.constraints.erase(model.constraints.begin() + j);

      changed = true;
    }
  }

  return changed;
}
// ============================================================
// DOMINATED COLUMNS
// ============================================================
//
// Conservative interpretation:
//
// A variable with:
//   1. no constraint contribution, and
//   2. no objective contribution
//
// can safely be removed.
//
// More aggressive LP column dominance requires
// objective-sense and bound reasoning and is
// intentionally outside this implementation.
// ============================================================

bool Presolver::removeDominatedColumns(model::Model &model,
                                       PresolveResult &result) {

  bool changed = false;

  std::vector<bool> used(model.variables.size(), false);

  for (const auto &constraint : model.constraints) {

    for (const auto &term : constraint.linearTerms) {

      if (term.variableIndex >= 0 &&
          static_cast<std::size_t>(term.variableIndex) < used.size()) {

        used[static_cast<std::size_t>(term.variableIndex)] = true;
      }
    }
  }

  for (const auto &term : model.objective.linearTerms) {

    if (term.variableIndex >= 0 &&
        static_cast<std::size_t>(term.variableIndex) < used.size()) {

      used[static_cast<std::size_t>(term.variableIndex)] = true;
    }
  }

  for (std::size_t i = model.variables.size(); i-- > 0;) {

    if (!used[i]) {

      Transformation transformation;

      transformation.type = TransformationType::RemoveVariable;

      transformation.index = i;

      transformation.reason = "Dominated empty column removed";

      result.transformations.push_back(std::move(transformation));

      removeVariable(model, i);

      changed = true;
    }
  }

  return changed;
}

// ============================================================
// PARALLEL COLUMNS
// ============================================================
//
// Detection only.
//
// We do not merge columns because a correct
// substitution must also transform:
//   - bounds
//   - objective
//   - all constraints
//   - reconstruction information
//
// Returning whether such columns exist is useful
// for diagnostics without performing an unsafe
// transformation.
// ============================================================

bool Presolver::processParallelColumns(model::Model &model,
                                       PresolveResult & /*result*/) {

  bool found = false;

  for (std::size_t i = 0; i < model.variables.size(); ++i) {

    for (std::size_t j = i + 1; j < model.variables.size(); ++j) {

      double scale = 0.0;
      bool initialized = false;
      bool parallel = true;

      for (const auto &constraint : model.constraints) {

        const double a = coefficientForVariable(constraint, i);

        const double b = coefficientForVariable(constraint, j);

        if (std::abs(a) <= EPS && std::abs(b) <= EPS) {

          continue;
        }

        if (std::abs(a) <= EPS || std::abs(b) <= EPS) {

          parallel = false;
          break;
        }

        const double currentScale = b / a;

        if (!initialized) {

          scale = currentScale;
          initialized = true;

        } else if (!approximatelyEqual(scale, currentScale)) {

          parallel = false;
          break;
        }
      }

      if (parallel && initialized) {
        found = true;
      }
    }
  }

  return found;
}

// ============================================================
// DEPENDENT EQUATIONS
// ============================================================

bool Presolver::equationsDependent(const model::Constraint &a,
                                   const model::Constraint &b) const {

  if (!approximatelyEqual(a.lowerBound, a.upperBound) ||
      !approximatelyEqual(b.lowerBound, b.upperBound)) {

    return false;
  }

  if (!constraintsParallel(a, b)) {
    return false;
  }

  const auto coefficientsA = buildCoefficientMap(a);

  const auto coefficientsB = buildCoefficientMap(b);

  double scale = 0.0;
  bool initialized = false;

  for (const auto &[index, valueA] : coefficientsA) {

    if (std::abs(valueA) <= EPS) {
      continue;
    }

    const auto it = coefficientsB.find(index);

    if (it == coefficientsB.end()) {
      return false;
    }

    const double currentScale = it->second / valueA;

    if (!initialized) {

      scale = currentScale;
      initialized = true;

    } else if (!approximatelyEqual(scale, currentScale)) {

      return false;
    }
  }

  if (!initialized || std::abs(scale) <= EPS) {

    return false;
  }

  // Check that the RHS scales in the same
  // way as the coefficients.
  const double expectedB = a.lowerBound * scale;

  return approximatelyEqual(expectedB, b.lowerBound);
}

// ============================================================
// REMOVE DEPENDENT EQUATIONS
// ============================================================

bool Presolver::detectDependentEquations(model::Model &model,
                                         PresolveResult &result) {

  bool changed = false;

  for (std::size_t i = 0; i < model.constraints.size(); ++i) {

    const auto &a = model.constraints[i];

    // Only equalities can be treated as dependent equations.
    if (!approximatelyEqual(a.lowerBound, a.upperBound)) {
      continue;
    }

    for (std::size_t j = model.constraints.size(); j-- > i + 1;) {

      const auto &b = model.constraints[j];

      if (!approximatelyEqual(b.lowerBound, b.upperBound)) {
        continue;
      }

      if (!constraintsParallel(a, b)) {
        continue;
      }

      // ------------------------------------------------------
      // Find scaling factor:
      //
      // b = scale * a
      // ------------------------------------------------------

      double scale = 0.0;
      bool foundScale = false;

      for (const auto &termA : a.linearTerms) {

        if (termA.variableIndex < 0) {
          continue;
        }

        const double coefficientB = coefficientForVariable(
            b, static_cast<std::size_t>(termA.variableIndex));

        if (std::abs(termA.value) <= EPS) {
          continue;
        }

        scale = coefficientB / termA.value;
        foundScale = true;
        break;
      }

      if (!foundScale || std::abs(scale) <= EPS) {
        continue;
      }

      // ------------------------------------------------------
      // Check that the bounds are scaled consistently.
      //
      // If:
      //
      //     b = scale * a
      //
      // then:
      //
      //     lowerB = scale * lowerA
      //     upperB = scale * upperA
      //
      // For negative scale the bounds reverse.
      // ------------------------------------------------------

      double expectedLower;
      double expectedUpper;

      if (scale > 0.0) {

        expectedLower = scale * a.lowerBound;
        expectedUpper = scale * a.upperBound;

      } else {

        expectedLower = scale * a.upperBound;
        expectedUpper = scale * a.lowerBound;
      }

      if (!approximatelyEqual(b.lowerBound, expectedLower) ||
          !approximatelyEqual(b.upperBound, expectedUpper)) {

        continue;
      }

      // ------------------------------------------------------
      // b is a scaled copy of a.
      // Remove b.
      // ------------------------------------------------------

      Transformation transformation;

      transformation.type = TransformationType::RemoveConstraint;

      transformation.index = j;

      transformation.reason = "Dependent equality removed";

      result.transformations.push_back(std::move(transformation));

      model.constraints.erase(model.constraints.begin() + j);

      changed = true;
    }
  }

  return changed;
}

// ============================================================
// MAIN PRESOLVE PIPELINE
// ============================================================

PresolveResult Presolver::run(const model::Model &input) {

  PresolveResult result;

  // Never modify caller's model.
  result.model = input;

  result.originalVariables = input.variables.size();

  result.originalConstraints = input.constraints.size();

  // ==========================================================
  // 1. ZERO COEFFICIENTS
  // ==========================================================

  removeZeroCoefficients(result.model);

  // ==========================================================
  // 2. EMPTY ROWS
  // ==========================================================

  for (std::size_t i = result.model.constraints.size(); i-- > 0;) {

    const auto &constraint = result.model.constraints[i];

    if (!constraint.linearTerms.empty()) {
      continue;
    }

    const bool satisfied =
        constraint.lowerBound <= 0.0 && 0.0 <= constraint.upperBound;

    Transformation transformation;

    transformation.type = TransformationType::RemoveConstraint;

    transformation.index = i;

    if (satisfied) {

      transformation.reason = "Empty row is redundant";

      result.transformations.push_back(std::move(transformation));

      result.model.constraints.erase(result.model.constraints.begin() + i);

    } else {

      result.infeasible = true;

      transformation.reason = "Empty row makes model infeasible";

      result.transformations.push_back(std::move(transformation));

      result.presolvedVariables = result.model.variables.size();

      result.presolvedConstraints = result.model.constraints.size();

      return result;
    }
  }

  // ==========================================================
  // 3. SIMPLE INFEASIBILITY
  // ==========================================================

  if (detectSimpleInfeasibility(result.model)) {

    result.infeasible = true;

    result.presolvedVariables = result.model.variables.size();

    result.presolvedConstraints = result.model.constraints.size();

    return result;
  }

  // ==========================================================
  // 4. EMPTY COLUMNS
  // ==========================================================

  std::vector<bool> variableUsedInConstraints(result.model.variables.size(),
                                              false);

  std::vector<bool> variableUsedInObjective(result.model.variables.size(),
                                            false);

  for (const auto &constraint : result.model.constraints) {

    for (const auto &term : constraint.linearTerms) {

      if (term.variableIndex >= 0 &&
          static_cast<std::size_t>(term.variableIndex) <
              variableUsedInConstraints.size()) {

        variableUsedInConstraints[static_cast<std::size_t>(
            term.variableIndex)] = true;
      }
    }
  }

  for (const auto &term : result.model.objective.linearTerms) {

    if (term.variableIndex >= 0 &&
        static_cast<std::size_t>(term.variableIndex) <
            variableUsedInObjective.size()) {

      variableUsedInObjective[static_cast<std::size_t>(term.variableIndex)] =
          true;
    }
  }

  for (std::size_t i = result.model.variables.size(); i-- > 0;) {

    if (!variableUsedInConstraints[i] && !variableUsedInObjective[i]) {

      Transformation transformation;

      transformation.type = TransformationType::RemoveVariable;

      transformation.index = i;

      transformation.reason = "Empty column with no objective contribution";

      result.transformations.push_back(std::move(transformation));

      removeVariable(result.model, i);
    }
  }

  // ==========================================================
  // 5. FIXED VARIABLES
  // ==========================================================

  for (std::size_t i = result.model.variables.size(); i-- > 0;) {

    const auto &variable = result.model.variables[i];

    if (!approximatelyEqual(variable.lowerBound, variable.upperBound)) {

      continue;
    }

    const double fixedValue = variable.lowerBound;

    Transformation transformation;

    transformation.type = TransformationType::FixVariable;

    transformation.index = i;

    transformation.oldValue = fixedValue;

    transformation.newValue = fixedValue;

    transformation.reason = "Variable is fixed by equal lower and upper bounds";

    result.transformations.push_back(std::move(transformation));

    fixVariable(result.model, i, fixedValue);
  }

  // ==========================================================
  // 6. SINGLETON ROWS
  // ==========================================================

  for (std::size_t i = result.model.constraints.size(); i-- > 0;) {

    if (result.model.constraints[i].linearTerms.size() != 1) {

      continue;
    }

    processSingletonRow(result.model, i, result);

    if (result.infeasible) {

      result.presolvedVariables = result.model.variables.size();

      result.presolvedConstraints = result.model.constraints.size();

      return result;
    }
  }

  // ==========================================================
  // 7. INFEASIBILITY AFTER SINGLETON PROCESSING
  // ==========================================================

  if (detectSimpleInfeasibility(result.model)) {

    result.infeasible = true;

    result.presolvedVariables = result.model.variables.size();

    result.presolvedConstraints = result.model.constraints.size();

    return result;
  }

  // ==========================================================
  // 8. BOUND TIGHTENING
  // ==========================================================

  bool boundsChanged = true;

  while (boundsChanged) {

    boundsChanged = tightenBounds(result.model, result);

    if (detectSimpleInfeasibility(result.model)) {

      result.infeasible = true;

      result.presolvedVariables = result.model.variables.size();

      result.presolvedConstraints = result.model.constraints.size();

      return result;
    }
  }

  // ==========================================================
  // 9. IMPLIED BOUNDS
  // ==========================================================

  applyImpliedBounds(result.model, result);

  if (detectSimpleInfeasibility(result.model)) {

    result.infeasible = true;

    result.presolvedVariables = result.model.variables.size();

    result.presolvedConstraints = result.model.constraints.size();

    return result;
  }

  // ----------------------------------------------------------
  // 10. Duplicate constraints
  // ----------------------------------------------------------

  removeDuplicateAndParallelRows(result.model, result);

  // ----------------------------------------------------------
  // 11. Dominated columns
  // ----------------------------------------------------------

  removeDominatedColumns(result.model, result);

  // ----------------------------------------------------------
  // 12. Parallel columns
  // ----------------------------------------------------------

  processParallelColumns(result.model, result);

  // ----------------------------------------------------------
  // 14 / 15. Dependent equations
  // ----------------------------------------------------------

  detectDependentEquations(result.model, result);

  if (detectSimpleInfeasibility(result.model)) {

    result.infeasible = true;
  }

  result.presolvedVariables = result.model.variables.size();

  result.presolvedConstraints = result.model.constraints.size();

  return result;
}

} // namespace presolve