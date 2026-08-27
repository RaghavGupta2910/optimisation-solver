#include "model/model.h"
#include "presolve/presolver.h"
#include <iostream>
#include <limits>

void printResult(const std::string &name,
                 const presolve::PresolveResult &result) {

  std::cout << "\n========================================\n";
  std::cout << name << "\n";
  std::cout << "========================================\n";

  std::cout << "Original variables: " << result.originalVariables << "\n";

  std::cout << "Presolved variables: " << result.presolvedVariables << "\n";

  std::cout << "Original constraints: " << result.originalConstraints << "\n";

  std::cout << "Presolved constraints: " << result.presolvedConstraints << "\n";

  std::cout << "Infeasible: " << std::boolalpha << result.infeasible << "\n";

  std::cout << "Transformations: " << result.transformations.size() << "\n";

  for (const auto &t : result.transformations) {

    std::cout << "  - Index: " << t.index << " | Reason: " << t.reason << "\n";
  }

  std::cout << "Objective offset: " << result.model.objective.offset << "\n";

  std::cout << "Variables:\n";

  for (const auto &v : result.model.variables) {

    std::cout << "  " << v.name << " [" << v.lowerBound << ", " << v.upperBound
              << "]\n";
  }

  std::cout << "Constraints:\n";

  for (const auto &c : result.model.constraints) {

    std::cout << "  " << c.name << " [" << c.lowerBound << ", " << c.upperBound
              << "]"
              << " | terms: " << c.linearTerms.size() << "\n";
  }
}

// ============================================================
// TEST 1 — Zero coefficients
// ============================================================

void testZeroCoefficients() {

  model::Model model;

  model.name = "zero_coeff_test";

  model.variables.push_back({"x", model::VariableType::Continuous, 0.0, 10.0});

  model.variables.push_back({"y", model::VariableType::Continuous, 0.0, 10.0});

  model.objective.linearTerms.push_back({0, 1.0});
  model.objective.linearTerms.push_back({1, 0.0});

  model::Constraint constraint;

  constraint.name = "c1";
  constraint.lowerBound = 5.0;
  constraint.upperBound = std::numeric_limits<double>::infinity();

  constraint.linearTerms.push_back({0, 1.0});
  constraint.linearTerms.push_back({1, 0.0});

  model.constraints.push_back(constraint);

  presolve::Presolver presolver;

  auto result = presolver.run(model);

  printResult("TEST 1 — ZERO COEFFICIENTS", result);
}

// ============================================================
// TEST 2 — Empty row, feasible
// ============================================================

void testEmptyRowFeasible() {

  model::Model model;

  model.name = "empty_row_feasible";

  model::Constraint constraint;

  constraint.name = "empty";
  constraint.lowerBound = -10.0;
  constraint.upperBound = 10.0;

  model.constraints.push_back(constraint);

  presolve::Presolver presolver;

  auto result = presolver.run(model);

  printResult("TEST 2 — EMPTY ROW FEASIBLE", result);
}

// ============================================================
// TEST 3 — Empty row, infeasible
// ============================================================

void testEmptyRowInfeasible() {

  model::Model model;

  model.name = "empty_row_infeasible";

  model::Constraint constraint;

  constraint.name = "empty";
  constraint.lowerBound = 5.0;
  constraint.upperBound = 10.0;

  model.constraints.push_back(constraint);

  presolve::Presolver presolver;

  auto result = presolver.run(model);

  printResult("TEST 3 — EMPTY ROW INFEASIBLE", result);
}

// ============================================================
// TEST 4 — Empty column
// ============================================================

void testEmptyColumn() {

  model::Model model;

  model.name = "empty_column_test";

  model.variables.push_back({"x", model::VariableType::Continuous, 0.0, 10.0});

  model.variables.push_back(
      {"unused", model::VariableType::Continuous, 0.0, 10.0});

  model.objective.linearTerms.push_back({0, 1.0});

  model::Constraint constraint;

  constraint.name = "c1";
  constraint.lowerBound = 5.0;
  constraint.upperBound = std::numeric_limits<double>::infinity();

  constraint.linearTerms.push_back({0, 1.0});

  model.constraints.push_back(constraint);

  presolve::Presolver presolver;

  auto result = presolver.run(model);

  printResult("TEST 4 — EMPTY COLUMN", result);
}

// ============================================================
// TEST 5 — Fixed variable
// ============================================================

void testFixedVariable() {

  model::Model model;

  model.name = "fixed_variable_test";

  model.variables.push_back({"x", model::VariableType::Continuous, 0.0, 100.0});

  model.variables.push_back({"y", model::VariableType::Continuous, 5.0, 5.0});

  model.objective.linearTerms.push_back({0, 1.0});
  model.objective.linearTerms.push_back({1, 2.0});

  model::Constraint constraint;

  constraint.name = "demand";
  constraint.lowerBound = 10.0;
  constraint.upperBound = std::numeric_limits<double>::infinity();

  constraint.linearTerms.push_back({0, 1.0});
  constraint.linearTerms.push_back({1, 1.0});

  model.constraints.push_back(constraint);

  presolve::Presolver presolver;

  auto result = presolver.run(model);

  printResult("TEST 5 — FIXED VARIABLE", result);
}

// ============================================================
// MAIN
// ============================================================

int main() {

  testZeroCoefficients();

  testEmptyRowFeasible();

  testEmptyRowInfeasible();

  testEmptyColumn();

  testFixedVariable();

  return 0;
}