#include "model/model.h"

namespace model {

bool Model::validate() const {

  // Check variable bounds
  for (const auto &variable : variables) {
    if (variable.lowerBound > variable.upperBound) {
      return false;
    }

    // Binary variables must stay within [0, 1]
    if (variable.type == VariableType::Binary) {
      if (variable.lowerBound < 0.0 || variable.upperBound > 1.0) {
        return false;
      }
    }
  }

  // Check constraints
  for (const auto &constraint : constraints) {

    if (constraint.lowerBound > constraint.upperBound) {
      return false;
    }

    // Check all linear-term variable indices
    for (const auto &term : constraint.linearTerms) {
      if (term.variableIndex < 0 ||
          term.variableIndex >= static_cast<int>(variables.size())) {
        return false;
      }
    }
  }

  // Check objective variable indices
  for (const auto &term : objective.linearTerms) {
    if (term.variableIndex < 0 ||
        term.variableIndex >= static_cast<int>(variables.size())) {
      return false;
    }
  }

  // Check quadratic objective variable indices
  for (const auto &term : objective.quadraticTerms) {
    if (term.variableIndex1 < 0 ||
        term.variableIndex1 >= static_cast<int>(variables.size()) ||
        term.variableIndex2 < 0 ||
        term.variableIndex2 >= static_cast<int>(variables.size())) {
      return false;
    }
  }

  return true;
}

} // namespace model