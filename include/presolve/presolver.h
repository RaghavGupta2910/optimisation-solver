#pragma once

#include "model/model.h"
#include "presolve/presolve_result.h"

namespace presolve {

class Presolver {
public:
  PresolveResult run(const model::Model &input);

private:
  void removeVariable(model::Model &model, std::size_t variableIndex);
  void removeZeroCoefficients(model::Model &model);
  bool fixVariable(model::Model &model, std::size_t variableIndex,
                   double value);
};

} // namespace presolve