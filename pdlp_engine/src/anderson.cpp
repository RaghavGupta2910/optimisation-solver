#include "pdlp/anderson.h"

#include <algorithm>
#include <cmath>

namespace pdlp {
namespace {

double dot(
    const std::vector<double>& left,
    const std::vector<double>& right
) noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        sum += left[i] * right[i];
    }
    return sum;
}

// Solves the small symmetric positive definite system G a = b in place by
// Cholesky. Returns false if the factorisation breaks down, which is the signal
// that the history has become linearly dependent.
bool solveSymmetric(std::vector<double>& matrix, std::vector<double>& vector, std::size_t n) {
    for (std::size_t j = 0; j < n; ++j) {
        double diagonal = matrix[j * n + j];
        for (std::size_t k = 0; k < j; ++k) {
            diagonal -= matrix[j * n + k] * matrix[j * n + k];
        }
        if (!(diagonal > 1e-300)) {
            return false;
        }
        diagonal = std::sqrt(diagonal);
        matrix[j * n + j] = diagonal;
        for (std::size_t i = j + 1; i < n; ++i) {
            double value = matrix[i * n + j];
            for (std::size_t k = 0; k < j; ++k) {
                value -= matrix[i * n + k] * matrix[j * n + k];
            }
            matrix[i * n + j] = value / diagonal;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        double value = vector[i];
        for (std::size_t k = 0; k < i; ++k) {
            value -= matrix[i * n + k] * vector[k];
        }
        vector[i] = value / matrix[i * n + i];
    }
    for (std::size_t i = n; i-- > 0;) {
        double value = vector[i];
        for (std::size_t k = i + 1; k < n; ++k) {
            value -= matrix[k * n + i] * vector[k];
        }
        vector[i] = value / matrix[i * n + i];
    }
    return true;
}

}  // namespace

AndersonAccelerator::AndersonAccelerator(int numColumns, int numRows, int depth)
    : columns_(numColumns),
      rows_(numRows),
      depth_(static_cast<std::size_t>(std::max(depth, 1))) {}

void AndersonAccelerator::record(
    const std::vector<double>& iteratePrimal,
    const std::vector<double>& iterateDual,
    const std::vector<double>& imagePrimal,
    const std::vector<double>& imageDual,
    const std::vector<double>& imageActivity
) {
    Entry entry;
    entry.imagePrimal = imagePrimal;
    entry.imageDual = imageDual;
    entry.imageActivity = imageActivity;

    entry.residualPrimal.resize(static_cast<std::size_t>(columns_));
    for (std::size_t j = 0; j < entry.residualPrimal.size(); ++j) {
        entry.residualPrimal[j] = imagePrimal[j] - iteratePrimal[j];
    }
    entry.residualDual.resize(static_cast<std::size_t>(rows_));
    for (std::size_t i = 0; i < entry.residualDual.size(); ++i) {
        entry.residualDual[i] = imageDual[i] - iterateDual[i];
    }

    entries_.push_back(std::move(entry));
    if (entries_.size() > depth_) {
        entries_.erase(entries_.begin());
    }
}

bool AndersonAccelerator::extrapolate(
    std::vector<double>& primal,
    std::vector<double>& dual,
    std::vector<double>& rowActivity
) {
    const std::size_t count = entries_.size();
    if (count < 2) {
        return false;
    }

    // Constrained least squares over the simplex-affine set sum(alpha) = 1,
    // reduced to an unconstrained problem in the residual differences
    // d_i = f_i - f_last, then mapped back.
    const std::size_t reduced = count - 1;
    std::vector<double> gram(reduced * reduced, 0.0);
    std::vector<double> rhs(reduced, 0.0);

    const Entry& last = entries_.back();
    std::vector<std::vector<double>> differencePrimal(reduced);
    std::vector<std::vector<double>> differenceDual(reduced);
    for (std::size_t i = 0; i < reduced; ++i) {
        differencePrimal[i].resize(static_cast<std::size_t>(columns_));
        for (std::size_t j = 0; j < differencePrimal[i].size(); ++j) {
            differencePrimal[i][j] =
                entries_[i].residualPrimal[j] - last.residualPrimal[j];
        }
        differenceDual[i].resize(static_cast<std::size_t>(rows_));
        for (std::size_t j = 0; j < differenceDual[i].size(); ++j) {
            differenceDual[i][j] = entries_[i].residualDual[j] - last.residualDual[j];
        }
    }

    double trace = 0.0;
    for (std::size_t i = 0; i < reduced; ++i) {
        for (std::size_t k = i; k < reduced; ++k) {
            const double value = dot(differencePrimal[i], differencePrimal[k]) +
                dot(differenceDual[i], differenceDual[k]);
            gram[i * reduced + k] = value;
            gram[k * reduced + i] = value;
        }
        rhs[i] = -(dot(differencePrimal[i], last.residualPrimal) +
                   dot(differenceDual[i], last.residualDual));
        trace += gram[i * reduced + i];
    }
    if (!(trace > 0.0)) {
        return false;
    }

    // Tikhonov term: the history goes linearly dependent as the iterates
    // converge, and an unregularised solve then produces enormous coefficients
    // that amplify rounding into the extrapolated point.
    const double regularisation = 1e-10 * trace / static_cast<double>(reduced);
    for (std::size_t i = 0; i < reduced; ++i) {
        gram[i * reduced + i] += regularisation;
    }
    if (!solveSymmetric(gram, rhs, reduced)) {
        return false;
    }

    weights_.assign(count, 0.0);
    double lastWeight = 1.0;
    for (std::size_t i = 0; i < reduced; ++i) {
        if (!std::isfinite(rhs[i])) {
            return false;
        }
        weights_[i] = rhs[i];
        lastWeight -= rhs[i];
    }
    weights_[count - 1] = lastWeight;

    // Reject wild extrapolations outright rather than letting the safeguard pay
    // a matrix pass to discover them.
    double magnitude = 0.0;
    for (const double weight : weights_) {
        magnitude += std::abs(weight);
    }
    if (!std::isfinite(magnitude) || magnitude > 1e4) {
        return false;
    }

    primal.assign(static_cast<std::size_t>(columns_), 0.0);
    dual.assign(static_cast<std::size_t>(rows_), 0.0);
    rowActivity.assign(static_cast<std::size_t>(rows_), 0.0);
    for (std::size_t i = 0; i < count; ++i) {
        const double weight = weights_[i];
        if (weight == 0.0) {
            continue;
        }
        const Entry& entry = entries_[i];
        for (std::size_t j = 0; j < primal.size(); ++j) {
            primal[j] += weight * entry.imagePrimal[j];
        }
        for (std::size_t j = 0; j < dual.size(); ++j) {
            dual[j] += weight * entry.imageDual[j];
            rowActivity[j] += weight * entry.imageActivity[j];
        }
    }
    return true;
}

void AndersonAccelerator::reset() noexcept {
    entries_.clear();
    weights_.clear();
}

}  // namespace pdlp
