#include "benchmark/solvercomparison.h"

// Intentionally minimal — solvercomparison.h is implemented as a
// header-only module (all functions are inline), so there is no
// additional logic to compile here. This file exists to verify
// the header compiles cleanly on its own and to match the
// include/X + src/X folder pattern used throughout this project.