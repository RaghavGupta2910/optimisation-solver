# PDLP CPU Engine v1

A self-contained C++17 primal-dual hybrid gradient (PDHG) engine for continuous
linear programs in the form

```text
minimize    c^T x + c0
subject to  row_lower <= A x <= row_upper
            var_lower <= x <= var_upper
```

No third-party dependencies: sparse storage, equilibration, the iteration
kernel, the threading runtime and the termination tests are all built here from
the mathematical foundations.

It deliberately does not include an MPS parser and does not depend on or modify
the repository's `model::Model`. Integration happens through a separate adapter
that converts `const model::Model&` into `pdlp::CompiledLp`.

## Components

| Component | File | Role |
|---|---|---|
| Sparse storage | `sparse_matrix.*` | Immutable CSR + CSC, O(nnz) construction, nnz-balanced execution plan |
| Threading | `parallel.*` | Persistent spin-then-yield worker pool |
| Equilibration | `scaling.*` | Ruiz row/column infinity-norm scaling |
| Preconditioner | `preconditioner.*` | Pock-Chambolle diagonal scaling, power-iteration spectral estimate |
| Iteration kernel | `pdhg_kernel.*` | Fused row-bound PDHG step, no slack variables |
| Step control | `step_controller.*` | Bounded step adaptation, restart-driven primal weight |
| Averaging | `iterate_average.*` | Weighted running mean of the iterate sequence |
| Restarts | `restart_controller.*` | Sufficient / necessary / artificial restart triggers |
| Termination | `termination.*` | Primal residual, dual residual, duality gap |
| Polishing | `feasibility_polishing.*` | Conservative KKT refinement |

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/pdlp_example
./build/pdlp_bench 200000 400000 10 500 8    # rows cols nnz/row iters threads
```

`CMAKE_BUILD_TYPE` defaults to `Release` if unset, because an unoptimised build
of this engine is roughly twenty times slower and that is easy to mistake for
the solver being slow.

Without CMake:

```bash
g++ -std=c++17 -O3 -pthread -I include src/*.cpp tests/test_pdlp.cpp -o pdlp_tests
./pdlp_tests
```

## Step size policy

The step size is chosen by the PDLP adaptive linesearch, not pinned to the
Pock-Chambolle bound `eta <= 1/||A||`. That bound is worst case over all
directions; the iterate moves in one direction, where the effective curvature is
routinely far milder. After each trial step the linesearch measures the curvature
that step actually encountered,

```text
eta_bar = ||dz||^2_omega / (2 |dy^T A dx|),
||dz||^2_omega = omega * sum_j dx_j^2 / T_j + (1/omega) * sum_i dy_i^2 / Sigma_i
```

accepts if `eta <= eta_bar`, and updates
`eta <- min{(1 - (k+1)^-0.3) eta_bar, (1 + (k+1)^-0.6) eta}`.

Two details matter. The movement is measured in the inverse-preconditioner norm,
because that is the norm the PDHG descent condition is stated in; using the plain
Euclidean norm makes the bound wrong whenever a preconditioner is active. And
`dy^T A dx` costs nothing extra: the iteration carries `A*x^k`, computes `A*x'`,
and recovers both the extrapolated activity and `A*dx` by subtraction.

Measured on the 20-instance family, the linesearch runs steps about **1.9x above
the static bound** and rejects almost nothing (1.01 trials per accepted
iteration).

## Performance

Apple M3 (4 performance + 4 efficiency cores), clang 15, `-O2`. Synthetic sparse
LPs, 10 nonzeros per row, time per PDHG iteration (lower is better):

| Problem | Before | 1 thread | 8 threads | Total |
|---|---|---|---|---|
| 20k x 40k, 200k nnz | 1154 us | 455 us | 109 us | 10.6x |
| 60k x 120k, 600k nnz | 3678 us | 1685 us | 374 us | 9.8x |
| 200k x 400k, 2M nnz | 13348 us | 6303 us | 1694 us | 7.9x |

Where the single-thread gain comes from:

- residual and movement norms accumulate sums of squares instead of folding
  through `std::hypot`, which is a non-vectorisable libm call costing about
  20 ns per element and dominated the original iteration;
- the sparse products are fused into the proximal steps, so `A^T y` and `A x'`
  are consumed coordinate-wise and never materialised;
- the termination check caches its problem-invariant normalisers and allocates
  no temporaries, instead of allocating three vectors and rebuilding two norms
  on every check;
- the dual proximal step is branchless and division-free;
- `fromTriplets` uses counting sort, O(nnz) rather than a comparison sort over
  16-byte triplets.

Parallel work is claimed dynamically from nonzero-balanced chunks rather than
statically partitioned. On a hybrid CPU a static split makes every barrier wait
on an efficiency core; with dynamic claiming, 8 threads went from slower than 4
to the fastest configuration.

Sparse matrix-vector products are bitwise identical between the serial and
parallel paths. Reductions are not: partial sums combine in worker order, so
residuals can differ in the last bits with thread count.

## Convergence

Work to reach a 1e-8 KKT tolerance, geometric mean over the 20-instance family,
measured in matrix passes. Trials are the unit rather than iterations because a
rejected linesearch trial costs a full pass, so a policy cannot win by trading
iterations for trials.

| Configuration | Matrix passes | Solved |
|---|---|---|
| bounded ratchet (the original policy) | budget exhausted | 0/20 |
| fixed step at the Pock-Chambolle bound | 15756 | 9/20 |
| adaptive linesearch | 9007 | 11/20 |
| adaptive linesearch, restart floor 64 | **8631** | **12/20** |

The original bounded ratchet was not merely conservative, it was harmful: it
drove the step down to its floor, about 1% of the static bound, and never
converged on anything. Most of the improvement above comes from removing it;
the linesearch then buys a further 1.8x over a fair fixed-step baseline.

Against HiGHS on the same family, at a 300000-iteration budget: 18 of 20
instances reach `optimal`, worst relative objective difference 3.4e-6, and on the
harder instances the engine is faster in wall clock (instance_17: 0.82 s against
16.8 s; instance_13: 0.46 s against 2.79 s). This is the regime the engine is
built for. It is not the regime of a few hundred rows solved to 1e-9, where
simplex terminates finitely at a vertex and a first-order method cannot compete
on principle.

## Measured negatives

Recorded so they are not re-attempted without new evidence. Both remain
implemented behind default-off options.

**Halpern acceleration** (`useHalpern`), `z^{k+1} = lambda_k T(z^k) + (1-lambda_k) z^anchor`
with `lambda_k = (k+1)/(k+2)` from the last restart. Slower than the plain step
in every regime tested: 10800 passes against 9007 on the 20-instance family, and
worse at every restart frequency from none at all through a 5000-iteration floor,
with and without averaging, and with a fixed step as well as the linesearch. The
published gains for restarted Halpern PDHG are against averaged PDHG under a
restart criterion built on the fixed-point residual; this engine restarts on the
KKT score and already restarts often, which is the most likely explanation. Worth
retesting if the restart criterion changes.

## Numerical convention

Saddle-point sign convention `stationarity = c + A^T y`. For an equality row
`A*x = b` the dual update is `y_next = y + sigma * (A*x_bar - b)`. Changing one
sign without changing the other is a solver bug.

The dual proximal step applies the Moreau identity for the support function of
`[l, u]`, written as

```text
y_next = max(v - sigma*u, 0) + min(v - sigma*l, 0)
```

which is exact on all three cases including equality rows and infinite bounds.
It returns exactly `0.0` on an inactive row, where the equivalent
`v - sigma*clip(v/sigma, l, u)` leaves rounding residue that accumulates into
the dual objective.

## Equilibration and the termination contract

Ruiz equilibration runs before the solve. The iteration runs on the equilibrated
problem; **termination is always evaluated on the original problem**, so the
tolerances a caller asks for are the tolerances they get, and the returned
primal and dual vectors are in the caller's coordinates.

This matters more than it sounds. Residuals are normalised by global norms, so
on a matrix whose entries span several orders of magnitude the large rows inflate
the normaliser until the small rows' violations disappear below tolerance. On a
test instance with entries from 5e-4 to 1e3, the unequilibrated engine reported a
relative primal residual of 2.7e-6 — apparent convergence — for a point whose
worst absolute constraint violation was 0.08, and whose objective was 0.8% below
the true optimum because it was infeasible. Equilibration removes that failure
mode; `testIllConditionedProblem` guards it by asserting absolute feasibility.

The dual objective is a genuine bound. Reduced costs that cannot be bounded
below over the variable box, and dual values outside the domain of the row
support function, are projected onto the dual-feasible cone and the projection
distance is reported as dual infeasibility. An earlier formulation discarded
reduced costs below a tolerance, which produced a "bound" that was not one.

## Validation

`tools/validate_against_reference.cpp` generates LP families (equality-heavy,
ill-conditioned, free-variable) and writes each instance plus this engine's
answer to disk. `tools/reference_check.py` re-solves them with HiGHS through
`scipy.optimize.milp` and compares objectives.

Current status on that family: agreement with HiGHS to 7e-6 relative on
well-conditioned instances, worst case 3.3e-3 on the ill-conditioned ones, where
the engine correctly reports `iteration_limit` rather than claiming optimality.

The unit tests cover CSR/CSC adjoint consistency, unsorted and cancelling
triplets, empty rows and columns, serial/parallel product equality, equality,
one-sided and ranged rows, exact-zero duals on inactive rows, an ill-conditioned
instance checked for absolute feasibility, a degenerate instance, rejection of
invalid problems, time-limit compliance, and thread-count independence. The
suite is clean under ThreadSanitizer, AddressSanitizer and UndefinedBehaviorSanitizer.

**Safeguarded Anderson acceleration** (`useAnderson`), type-II on the PDHG
fixed-point map with a Cholesky solve of the regularised normal equations and a
residual safeguard.

| Configuration | Matrix passes | Solved | Wall time |
|---|---|---|---|
| adaptive linesearch | **8631** | **12/20** | **4.7 s** |
| fixed step | 15212 | 10/20 | 5.4 s |
| fixed step + Anderson, depth 5 | 12089 | 9/20 | 39.1 s |
| linesearch + Anderson, depth 5 | 17926 | 10/20 | 41.2 s |

The mechanism works where its assumptions hold. Anderson presumes a fixed
operator `T`; against a fixed step, where that is true, depth 5 cuts matrix
passes by 1.26x, which is evidence the implementation is sound rather than
merely inert. It still loses on all three counts that matter. It cannot approach
the linesearch (12089 against 8631), and combined with the linesearch it is worse
than either alone, because an adaptive step changes `T` every iteration and the
history then mixes samples from different operators. Wall time is eight times the
baseline: the safeguard costs an extra pass over the matrix per iteration, and
the history dot products are O(depth * (n + m)) on top of that.


## Known limitations

These are real and should be read before quoting this engine against a
commercial solver.

1. **No infeasibility or unboundedness certificate.** An unbounded or infeasible
   LP runs to the iteration limit. The engine never reports `Optimal` in those
   cases (`testUnboundedIsNotReportedOptimal` guards this), but it cannot tell
   the caller *why* it failed to converge. Farkas-ray detection from the
   difference of successive iterates is the next correctness item, and is now the
   highest-value remaining work.

2. **Restarts trigger on the KKT score, not the normalised duality gap.** The
   linear-convergence theory for restarted PDHG is stated for the localised
   normalised duality gap `mu_r(z)`, which has a sharpness property near the
   solution set that the raw residual lacks. Computing `mu_r` exactly is a
   trust-region subproblem: it separates coordinate-wise given a Lagrange
   multiplier, so it costs a bisection with an O(n+m) evaluation per step. That
   is affordable at the current check frequency and is the main algorithmic item
   still outstanding.

3. **No presolve.** Reversible presolve and postsolve are outside this engine and
   owned elsewhere.

4. **Feasibility polishing is a placeholder.** `FeasibilityPolisher` performs
   conservative small-step KKT refinement, not the paper's feasibility polishing.
   It is replaceable without changing the solver interface.

5. **Not benchmarked on Netlib, MIPLIB or Mittelmann.** Validation uses synthetic
   families and HiGHS as a reference. The standard sets need an MPS reader in the
   adapter layer, which is deliberately outside this module.

6. **No GPU path.** The kernel is deliberately shaped for one: the two fused
   halves are coordinate-parallel with a single barrier between them, which maps
   onto two CUDA kernels, and the linesearch's reductions are the only device
   synchronisation points per iteration. Nothing here targets a GPU yet.

7. **Anderson and Halpern are implemented but off.** See "Measured negatives".
   Both cost nothing when disabled.

## Integration boundary

The model adapter must:

1. convert maximisation to minimisation;
2. copy the objective and objective offset;
3. copy variable and row bounds;
4. convert `Constraint.linearTerms` into matrix triplets;
5. build `SparseMatrix::fromTriplets()`;
6. preserve original row/column index mappings for later postsolve.

Do not add MPS parsing logic to this module.
