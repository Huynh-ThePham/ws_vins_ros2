#pragma once

// GeoDF fundamental-matrix degeneracy guard (plan P1.7 / P0 design-matrix fix).
//
// GeoDF only checked F.empty(). A fundamental matrix estimated during pure rotation,
// at low parallax, from features clustered in one image region, or from a frame the
// movers dominate, is not empty -- it is confidently wrong. Hard-rejecting static
// structure on that basis is exactly the failure mode the paper claims to avoid.
//
// When the geometry is degenerate:
//   - GeoDF must not hard-reject;
//   - GeoDF must not count as strong agreement evidence;
//   - only a diagnostic residual or a mild down-weight is justified.
//
// Design-matrix conditioning (NOT residual median/MAD):
//   1. Hartley-normalize both correspondence sets.
//   2. Build the normalized eight-point design matrix A (N x 9), one row per match:
//        [x'x, x'y, x', y'x, y'y, y', x, y, 1]
//   3. SVD: A = U Σ Vᵀ with σ₁ ≥ … ≥ σ₉ ≥ 0.
//   4. Report:
//        κ_eff = σ₁ / max(σ₈, ε)     // effective condition of the rank-8 subspace
//        g_F   = σ₈ / max(σ₉, ε)     // nullspace gap of the F solution
//   Sampson median/MAD remain separate residual diagnostics. They must never be
//   named or gated as a design-matrix condition number.

#include <Eigen/Core>
#include <Eigen/SVD>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace geodf_degeneracy
{

enum class Health
{
    Healthy = 0,   // hard rejection permitted
    Weak,          // down-weight only, not strong agreement evidence
    Degenerate,    // no GeoDF decision at all
};

inline const char *toString(Health health)
{
    switch (health)
    {
        case Health::Healthy:    return "healthy";
        case Health::Weak:       return "weak";
        case Health::Degenerate: return "degenerate";
    }
    return "unknown";
}

enum class Cause
{
    NONE = 0,
    NO_FUNDAMENTAL,
    LOW_GRID_OCCUPANCY,
    LOW_PARALLAX,
    ILL_CONDITIONED,
    FEW_INLIERS,
    LOW_INLIER_RATIO,
    MOVER_DOMINANT,
    UNKNOWN_CONDITIONING,  // insufficient / non-finite design metrics (fail-closed)
};

inline const char *toString(Cause cause)
{
    switch (cause)
    {
        case Cause::NONE:                  return "NONE";
        case Cause::NO_FUNDAMENTAL:        return "NO_FUNDAMENTAL";
        case Cause::LOW_GRID_OCCUPANCY:    return "LOW_GRID_OCCUPANCY";
        case Cause::LOW_PARALLAX:          return "LOW_PARALLAX";
        case Cause::ILL_CONDITIONED:       return "ILL_CONDITIONED";
        case Cause::FEW_INLIERS:           return "FEW_INLIERS";
        case Cause::LOW_INLIER_RATIO:      return "LOW_INLIER_RATIO";
        case Cause::MOVER_DOMINANT:        return "MOVER_DOMINANT";
        case Cause::UNKNOWN_CONDITIONING:  return "UNKNOWN_CONDITIONING";
    }
    return "UNKNOWN";
}

struct Config
{
    double min_grid_occupancy = 0.35;
    double min_median_parallax_px = 1.0;
    // Threshold on κ_eff = σ₁/σ₈ of the normalized eight-point design matrix.
    double max_effective_design_condition = 1.0e6;
    // Kept as alias for YAML key geodf_max_design_condition_number (same meaning now).
    double max_design_condition_number = 1.0e6;
    int min_ransac_inliers = 20;
    double min_ransac_inlier_ratio = 0.35;
    double max_mover_share = 0.60;
    // Publication: insufficient design metrics => Degenerate, never "Healthy".
    bool require_known_conditioning = true;
};

struct Observation
{
    bool fundamental_valid = false;
    double grid_occupancy = 0.0;
    double median_parallax_px = 0.0;

    // Residual diagnostics (NOT a design condition number).
    double sampson_median = 0.0;
    double sampson_mad = 0.0;

    // Normalized eight-point design-matrix metrics. Valid only if design_metrics_valid.
    double effective_design_condition = 0.0;  // κ_eff = σ₁ / max(σ₈, ε)
    double nullspace_gap = 0.0;               // g_F   = σ₈ / max(σ₉, ε)
    bool design_metrics_valid = false;

    // Deprecated alias kept so existing call sites that still set
    // design_condition_number compile during migration; evaluate() prefers
    // effective_design_condition when design_metrics_valid.
    double design_condition_number = 0.0;

    int ransac_inliers = 0;
    int ransac_total = 0;
    double mover_share = 0.0;
};

struct Result
{
    Health health = Health::Healthy;
    Cause cause = Cause::NONE;
    double conditioning = 0.0;
    double inlier_ratio = 0.0;

    bool mayHardReject() const { return health == Health::Healthy; }
    bool mayCountAsStrongAgreement() const { return health == Health::Healthy; }
};

struct DesignMatrixMetrics
{
    bool valid = false;
    double sampson_median = 0.0;
    double sampson_mad = 0.0;
    double sigma1 = 0.0;
    double sigma8 = 0.0;
    double sigma9 = 0.0;
    double effective_design_condition = 0.0;  // κ_eff
    double nullspace_gap = 0.0;               // g_F
    int correspondence_count = 0;
};

inline double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const size_t mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const double hi = values[mid];
    if (values.size() % 2 == 1)
        return hi;
    std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
    return 0.5 * (hi + values[mid - 1]);
}

inline double medianAbsoluteDeviation(const std::vector<double> &values)
{
    if (values.empty())
        return 0.0;
    const double m = median(values);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double v : values)
        deviations.push_back(std::abs(v - m));
    return median(std::move(deviations));
}

inline double gridOccupancy(const std::vector<std::pair<double, double>> &points,
                            double width, double height, int cols = 6, int rows = 4)
{
    if (points.empty() || width <= 0.0 || height <= 0.0 || cols <= 0 || rows <= 0)
        return 0.0;
    std::vector<char> occupied(static_cast<size_t>(cols) * rows, 0);
    for (const auto &p : points)
    {
        if (!std::isfinite(p.first) || !std::isfinite(p.second))
            continue;
        int cx = static_cast<int>(p.first / width * cols);
        int cy = static_cast<int>(p.second / height * rows);
        cx = std::min(cols - 1, std::max(0, cx));
        cy = std::min(rows - 1, std::max(0, cy));
        occupied[static_cast<size_t>(cy) * cols + cx] = 1;
    }
    int count = 0;
    for (char c : occupied)
        count += c ? 1 : 0;
    return static_cast<double>(count) / static_cast<double>(cols * rows);
}

// Hartley isotropic normalization: translate centroid to origin, scale mean
// distance to sqrt(2). Returns false if the set is empty or has zero spread.
inline bool hartleyNormalize(const std::vector<Eigen::Vector2d> &pts,
                             std::vector<Eigen::Vector2d> &out,
                             Eigen::Matrix3d &T)
{
    out.clear();
    T = Eigen::Matrix3d::Identity();
    if (pts.size() < 2)
        return false;

    Eigen::Vector2d centroid = Eigen::Vector2d::Zero();
    int finite = 0;
    for (const auto &p : pts)
    {
        if (!p.allFinite())
            continue;
        centroid += p;
        finite++;
    }
    if (finite < 2)
        return false;
    centroid /= static_cast<double>(finite);

    double mean_dist = 0.0;
    for (const auto &p : pts)
    {
        if (!p.allFinite())
            continue;
        mean_dist += (p - centroid).norm();
    }
    mean_dist /= static_cast<double>(finite);
    if (!(mean_dist > 1e-12) || !std::isfinite(mean_dist))
        return false;

    const double scale = std::sqrt(2.0) / mean_dist;
    T << scale, 0.0, -scale * centroid.x(),
         0.0, scale, -scale * centroid.y(),
         0.0, 0.0, 1.0;

    out.reserve(pts.size());
    for (const auto &p : pts)
    {
        if (!p.allFinite())
            continue;
        out.emplace_back(scale * (p - centroid));
    }
    return out.size() >= 2;
}

// Compute Sampson residual diagnostics AND normalized eight-point design metrics
// from the same correspondence set used to estimate F.
//
// pts_cur / pts_prev are UNNORMALIZED image (or lifted-focal) coordinates as used
// by findFundamentalMat. Requires at least 8 finite pairs.
inline DesignMatrixMetrics computeDesignMatrixMetrics(
    const std::vector<Eigen::Vector2d> &pts_cur,
    const std::vector<Eigen::Vector2d> &pts_prev,
    const std::vector<double> &sampson_residuals)
{
    DesignMatrixMetrics m;
    m.sampson_median = median(sampson_residuals);
    m.sampson_mad = medianAbsoluteDeviation(sampson_residuals);
    m.correspondence_count = static_cast<int>(
        std::min(pts_cur.size(), pts_prev.size()));

    if (pts_cur.size() != pts_prev.size() || pts_cur.size() < 8)
        return m;

    std::vector<Eigen::Vector2d> cur_n, prev_n;
    Eigen::Matrix3d T1, T2;
    if (!hartleyNormalize(pts_cur, cur_n, T1) || !hartleyNormalize(pts_prev, prev_n, T2))
        return m;
    if (cur_n.size() != prev_n.size() || cur_n.size() < 8)
        return m;

    const int N = static_cast<int>(cur_n.size());
    Eigen::MatrixXd A(N, 9);
    for (int i = 0; i < N; i++)
    {
        const double x = cur_n[i].x();
        const double y = cur_n[i].y();
        const double xp = prev_n[i].x();  // x'
        const double yp = prev_n[i].y();  // y'
        // Row: [x'x, x'y, x', y'x, y'y, y', x, y, 1]
        A(i, 0) = xp * x;
        A(i, 1) = xp * y;
        A(i, 2) = xp;
        A(i, 3) = yp * x;
        A(i, 4) = yp * y;
        A(i, 5) = yp;
        A(i, 6) = x;
        A(i, 7) = y;
        A(i, 8) = 1.0;
    }

    if (!A.allFinite())
        return m;

    Eigen::JacobiSVD<Eigen::MatrixXd> svd(A, Eigen::ComputeThinU | Eigen::ComputeThinV);
    const Eigen::VectorXd &S = svd.singularValues();
    if (S.size() < 9 || !S.allFinite())
        return m;

    constexpr double kEps = 1e-12;
    m.sigma1 = S(0);
    m.sigma8 = S(7);
    m.sigma9 = S(8);
    if (!(m.sigma1 > 0.0) || !std::isfinite(m.sigma1))
        return m;

    m.effective_design_condition = m.sigma1 / std::max(m.sigma8, kEps);
    m.nullspace_gap = m.sigma8 / std::max(m.sigma9, kEps);
    if (!std::isfinite(m.effective_design_condition) || !std::isfinite(m.nullspace_gap))
        return m;
    if (m.effective_design_condition < 1.0)
        m.effective_design_condition = 1.0;

    m.valid = true;
    return m;
}

// Convenience overload from OpenCV-style Point2f pairs.
template <typename Point2>
inline DesignMatrixMetrics computeDesignMatrixMetricsFromPoints(
    const std::vector<Point2> &cur,
    const std::vector<Point2> &prev,
    const std::vector<double> &sampson_residuals)
{
    std::vector<Eigen::Vector2d> c, p;
    c.reserve(cur.size());
    p.reserve(prev.size());
    const size_t n = std::min(cur.size(), prev.size());
    for (size_t i = 0; i < n; i++)
    {
        c.emplace_back(cur[i].x, cur[i].y);
        p.emplace_back(prev[i].x, prev[i].y);
    }
    return computeDesignMatrixMetrics(c, p, sampson_residuals);
}

inline Result evaluate(const Observation &obs, const Config &config)
{
    Result out;
    out.inlier_ratio = obs.ransac_total > 0
                           ? static_cast<double>(obs.ransac_inliers) / obs.ransac_total
                           : 0.0;

    if (!obs.fundamental_valid)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::NO_FUNDAMENTAL;
        return out;
    }

    const double kappa = obs.design_metrics_valid
                             ? obs.effective_design_condition
                             : obs.design_condition_number;
    const double kappa_limit = config.max_effective_design_condition > 0.0
                                   ? config.max_effective_design_condition
                                   : config.max_design_condition_number;

    // Fail-closed: without a known design-matrix metric, never claim Healthy.
    if (config.require_known_conditioning && !obs.design_metrics_valid)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::UNKNOWN_CONDITIONING;
        out.conditioning = 0.0;
        return out;
    }

    const double occupancy_score =
        config.min_grid_occupancy > 0.0
            ? std::min(1.0, obs.grid_occupancy / config.min_grid_occupancy)
            : 1.0;
    const double parallax_score =
        config.min_median_parallax_px > 0.0
            ? std::min(1.0, obs.median_parallax_px / config.min_median_parallax_px)
            : 1.0;
    const double condition_score =
        (kappa_limit > 0.0 && kappa > 0.0)
            ? std::min(1.0, kappa_limit / kappa)
            : (obs.design_metrics_valid ? 0.0 : 1.0);
    const double inlier_score =
        config.min_ransac_inlier_ratio > 0.0
            ? std::min(1.0, out.inlier_ratio / config.min_ransac_inlier_ratio)
            : 1.0;
    out.conditioning = std::min(std::min(occupancy_score, parallax_score),
                                std::min(condition_score, inlier_score));
    out.conditioning = std::min(1.0, std::max(0.0, out.conditioning));

    if (obs.median_parallax_px < config.min_median_parallax_px)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::LOW_PARALLAX;
        return out;
    }
    if (obs.design_metrics_valid && kappa > kappa_limit && kappa_limit > 0.0)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::ILL_CONDITIONED;
        return out;
    }
    // Legacy path: if metrics missing but require_known_conditioning is off, still
    // honour an explicitly injected kappa (unit tests).
    if (!obs.design_metrics_valid && kappa > kappa_limit && kappa_limit > 0.0)
    {
        out.health = Health::Degenerate;
        out.cause = Cause::ILL_CONDITIONED;
        return out;
    }
    if (obs.grid_occupancy < config.min_grid_occupancy)
    {
        out.health = Health::Weak;
        out.cause = Cause::LOW_GRID_OCCUPANCY;
        return out;
    }
    if (obs.ransac_inliers < config.min_ransac_inliers)
    {
        out.health = Health::Weak;
        out.cause = Cause::FEW_INLIERS;
        return out;
    }
    if (out.inlier_ratio < config.min_ransac_inlier_ratio)
    {
        out.health = Health::Weak;
        out.cause = Cause::LOW_INLIER_RATIO;
        return out;
    }
    if (obs.mover_share > config.max_mover_share)
    {
        out.health = Health::Weak;
        out.cause = Cause::MOVER_DOMINANT;
        return out;
    }

    out.health = Health::Healthy;
    out.cause = Cause::NONE;
    return out;
}

}  // namespace geodf_degeneracy
