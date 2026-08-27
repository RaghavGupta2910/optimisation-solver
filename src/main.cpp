#include <iostream>

#include "model/model.h"

int main() {

  model::Model problem;

  problem.name = "test_lp";

  problem.objective.sense = model::ObjectiveSense::Minimize;

  problem.variables.push_back(
      {"x1", model::VariableType::Continuous, 0.0, 100.0});

  problem.variables.push_back(
      {"x2", model::VariableType::Continuous, 0.0, 100.0});

  problem.objective.linearTerms.push_back({0, 3.0});
  problem.objective.linearTerms.push_back({1, 5.0});

  model::Constraint c1;
  c1.name = "C1";
  c1.lowerBound = 4.0;
  c1.upperBound = 100.0;
  c1.linearTerms.push_back({0, 2.0});
  c1.linearTerms.push_back({1, 1.0});

  problem.constraints.push_back(c1);

  model::Constraint c2;
  c2.name = "C2";
  c2.lowerBound = 0.0;
  c2.upperBound = 6.0;
  c2.linearTerms.push_back({0, 1.0});
  c2.linearTerms.push_back({1, 3.0});

  problem.constraints.push_back(c2);

  std::cout << "Model: " << problem.name << '\n';
  std::cout << "Variables: " << problem.variables.size() << '\n';
  std::cout << "Constraints: " << problem.constraints.size() << '\n';

  std::cout << "Validation: " << (problem.validate() ? "PASSED" : "FAILED")
            << '\n';
  return 0;
}