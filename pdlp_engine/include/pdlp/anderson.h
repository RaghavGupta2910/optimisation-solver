#pragma once

#include "pdlp/pdlp_state.h"

#include <cstddef>
#include <vector>

namespace pdlp {

// Safeguarded type-II Anderson acceleration on the PDHG fixed-point map.
//
// PDHG is z <- T(z); the fixed-point residual is f(z) = T(z) - z. Anderson keeps
// a short history of (T(z_i), f_i) and extrapolates to the affine combination of
// past T-values whose residuals cancel best:
//
//   min || sum_i alpha_i f_i ||   subject to   sum_i alpha_i = 1
//   z_AA = sum_i alpha_i T(z_i)
//
// Unsafeguarded, this diverges on nonsmooth operators, which the LP prox map is.
// The safeguard evaluates T at the extrapolated point and keeps it only if its
// residual actually improves on the plain PDHG step. That check costs one extra
// pass over the matrix per iteration, so acceleration has to more than halve the
// iteration count merely to break even.
//
// Row activity is carried through the same affine combination: A is linear, so
// the extrapolated point's A*x follows from the history's without a sparse
// product.
class AndersonAccelerator {
public:
    AndersonAccelerator(int numColumns, int numRows, int depth);

    // Records the plain PDHG image T(z) of the current iterate z.
    void record(
        const std::vector<double>& iteratePrimal,
        const std::vector<double>& iterateDual,
        const std::vector<double>& imagePrimal,
        const std::vector<double>& imageDual,
        const std::vector<double>& imageActivity
    );

    // Forms the extrapolated candidate. Returns false when the history is too
    // short or the least-squares system is too ill-conditioned to trust.
    [[nodiscard]] bool extrapolate(
        std::vector<double>& primal,
        std::vector<double>& dual,
        std::vector<double>& rowActivity
    );

    void reset() noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Entry {
        std::vector<double> imagePrimal;   // T(z)_x
        std::vector<double> imageDual;     // T(z)_y
        std::vector<double> imageActivity; // A * T(z)_x
        std::vector<double> residualPrimal;
        std::vector<double> residualDual;
    };

    int columns_ = 0;
    int rows_ = 0;
    std::size_t depth_ = 0;
    std::vector<Entry> entries_;
    std::vector<double> weights_;
};

}  // namespace pdlp
