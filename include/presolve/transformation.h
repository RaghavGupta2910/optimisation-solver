#pragma once

#include <cstddef>
#include <string>

namespace presolve {

enum class TransformationType {
  RemoveVariable,
  RemoveConstraint,
  FixVariable,
  SubstituteVariable,
  TightenLowerBound,
  TightenUpperBound
};

struct Transformation {
  TransformationType type;

  std::size_t index = 0;

  double oldValue = 0.0;
  double newValue = 0.0;

  std::string reason;
};

} // namespace presolve