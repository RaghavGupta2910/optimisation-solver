#include "presolve/presolver.h"

#include <algorithm>
#include <utility>

namespace presolve {
void Presolver::removeVariable(model::Model &model, std::size_t variableIndex) {

  // Remove the variable itself.
  model.variables.erase(model.variables.begin() + variableIndex);

  // Update objective term indices.
  for (auto &term : model.objective.linearTerms) {

    if (term.variableIndex > static_cast<int>(variableIndex)) {

      --term.variableIndex;
    }
  }

  // Remove objective terms referring to the deleted variable.
  model.objective.linearTerms.erase(
      std::remove_if(model.objective.linearTerms.begin(),
                     model.objective.linearTerms.end(),
                     [variableIndex](const model::LinearTerm &term) {
                       return term.variableIndex ==
                              static_cast<int>(variableIndex);
                     }),
      model.objective.linearTerms.end());

  // Update constraint term indices.
  for (auto &constraint : model.constraints) {

    for (auto &term : constraint.linearTerms) {

      if (term.variableIndex > static_cast<int>(variableIndex)) {

        --term.variableIndex;
      }
    }

    // Remove terms referring to the deleted variable.
    constraint.linearTerms.erase(
        std::remove_if(
            constraint.linearTerms.begin(), constraint.linearTerms.end(),
            [variableIndex](const model::LinearTerm &term) {
              return term.variableIndex == static_cast<int>(variableIndex);
            }),
        constraint.linearTerms.end());
  }
}

bool Presolver::fixVariable(model::Model &model, std::size_t variableIndex,
                            double value) {

  // ---------------------------------------------------------
  // Substitute the fixed value into the objective
  //
  // c*x becomes c*value, which is a constant.
  // Therefore it is added to the objective offset.
  // ---------------------------------------------------------

  for (auto it = model.objective.linearTerms.begin();
       it != model.objective.linearTerms.end();) {

    if (it->variableIndex == static_cast<int>(variableIndex)) {

      model.objective.offset += it->value * value;

      it = model.objective.linearTerms.erase(it);

    } else {

      ++it;
    }
  }

  // ---------------------------------------------------------
  // Substitute the fixed value into constraints
  //
  // For:
  //
  //     a*x + other_terms
  //
  // with x = value:
  //
  //     other_terms = bound - a*value
  //
  // Therefore both constraint bounds are shifted.
  // ---------------------------------------------------------

  for (auto &constraint : model.constraints) {

    for (auto it = constraint.linearTerms.begin();
         it != constraint.linearTerms.end();) {

      if (it->variableIndex == static_cast<int>(variableIndex)) {

        double contribution = it->value * value;

        // Move the fixed contribution to
        // the right-hand side.

        if (constraint.lowerBound != -std::numeric_limits<double>::infinity()) {

          constraint.lowerBound -= contribution;
        }

        if (constraint.upperBound != std::numeric_limits<double>::infinity()) {

          constraint.upperBound -= contribution;
        }

        it = constraint.linearTerms.erase(it);

      } else {

        ++it;
      }
    }
  }

  // ---------------------------------------------------------
  // Remove the variable from the model.
  //
  // removeVariable() also updates all variable indices.
  // ---------------------------------------------------------

  removeVariable(model, variableIndex);

  return true;
}

void Presolver::removeZeroCoefficients(model::Model &model) {

  // Remove zero coefficients from objective
  model.objective.linearTerms.erase(
      std::remove_if(
          model.objective.linearTerms.begin(),
          model.objective.linearTerms.end(),
          [](const model::LinearTerm &term) { return term.value == 0.0; }),
      model.objective.linearTerms.end());

  // Remove zero coefficients from constraints
  for (auto &constraint : model.constraints) {

    constraint.linearTerms.erase(
        std::remove_if(
            constraint.linearTerms.begin(), constraint.linearTerms.end(),
            [](const model::LinearTerm &term) { return term.value == 0.0; }),
        constraint.linearTerms.end());
  }
}

PresolveResult Presolver::run(const model::Model &input) {

  PresolveResult result;

  // Work on a copy. The original model is never modified.
  result.model = input;
  // Remove zero coefficients before other presolve passes.
  removeZeroCoefficients(result.model);

  result.originalVariables = input.variables.size();

  result.originalConstraints = input.constraints.size();

  // ---------------------------------------------------------
  // Empty row presolve
  // ---------------------------------------------------------

  std::vector<model::Constraint> remainingConstraints;

  for (std::size_t i = 0; i < result.model.constraints.size(); ++i) {

    const auto &constraint = result.model.constraints[i];

    // An empty row means:
    //
    // A*x = 0
    //
    // Therefore the constraint becomes:
    //
    // lowerBound <= 0 <= upperBound

    if (constraint.linearTerms.empty()) {

      bool satisfied =
          constraint.lowerBound <= 0.0 && 0.0 <= constraint.upperBound;

      if (satisfied) {

        // Redundant constraint.
        // Remove it from the presolved model.

        Transformation transformation;

        transformation.type = TransformationType::RemoveConstraint;

        transformation.index = i;

        transformation.reason = "Empty row is redundant";

        result.transformations.push_back(transformation);

        continue;
      }

      // Empty row cannot be satisfied.
      // Therefore the entire model is infeasible.

      result.infeasible = true;

      Transformation transformation;

      transformation.type = TransformationType::RemoveConstraint;

      transformation.index = i;

      transformation.reason = "Empty row makes model infeasible";

      result.transformations.push_back(transformation);

      return result;
    }

    // Normal constraint.
    remainingConstraints.push_back(constraint);
  }

  result.model.constraints = std::move(remainingConstraints);

  // ---------------------------------------------------------
  // Empty column presolve
  // ---------------------------------------------------------

  std::vector<bool> variableUsedInConstraints(result.model.variables.size(),
                                              false);

  std::vector<bool> variableUsedInObjective(result.model.variables.size(),
                                            false);

  // Check constraint matrix.
  for (const auto &constraint : result.model.constraints) {

    for (const auto &term : constraint.linearTerms) {

      if (term.variableIndex >= 0 &&
          static_cast<std::size_t>(term.variableIndex) <
              variableUsedInConstraints.size()) {

        variableUsedInConstraints[term.variableIndex] = true;
      }
    }
  }

  // Check objective.
  for (const auto &term : result.model.objective.linearTerms) {

    if (term.variableIndex >= 0 &&
        static_cast<std::size_t>(term.variableIndex) <
            variableUsedInObjective.size()) {

      variableUsedInObjective[term.variableIndex] = true;
    }
  }

  // Remove variables that appear nowhere.
  //
  // We iterate backwards because removing an element
  // changes the indices of elements after it.

  for (std::size_t i = result.model.variables.size(); i-- > 0;) {

    if (!variableUsedInConstraints[i] && !variableUsedInObjective[i]) {

      Transformation transformation;

      transformation.type = TransformationType::RemoveVariable;

      transformation.index = i;

      transformation.reason = "Empty column with no objective contribution";

      result.transformations.push_back(transformation);

      removeVariable(result.model, i);
    }
  }

  // ---------------------------------------------------------
  // Fixed variable presolve
  // ---------------------------------------------------------

  for (std::size_t i = result.model.variables.size(); i-- > 0;) {

    const auto &variable = result.model.variables[i];

    // A variable is fixed when:
    //
    // lowerBound == upperBound
    //
    // Example:
    //     5 <= y <= 5
    //
    // therefore:
    //     y = 5

    if (variable.lowerBound == variable.upperBound) {

      double fixedValue = variable.lowerBound;

      Transformation transformation;

      transformation.type = TransformationType::FixVariable;

      transformation.index = i;

      transformation.oldValue = fixedValue;
      transformation.newValue = fixedValue;

      transformation.reason =
          "Variable is fixed by equal lower and upper bounds";

      result.transformations.push_back(transformation);

      fixVariable(result.model, i, fixedValue);
    }
  }

  // ---------------------------------------------------------
  // Final statistics
  // ---------------------------------------------------------

  result.presolvedVariables = result.model.variables.size();

  result.presolvedConstraints = result.model.constraints.size();

  return result;
}

} // namespace presolve